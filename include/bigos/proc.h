#ifndef _BIG_PROC_H
#define _BIG_PROC_H

#include <bigos/types.h>
#include <bigos/errno.h>
#include <bigos/fs/vfs.h>
#include <bigos/memory.h>
#include <bigos/signal.h>
#include <irq/interrupt.h>

namespace bigos::proc {
    constexpr uint64_t USER_CODE_BASE = 0x0000000000400000ull;
    constexpr uint64_t USER_DATA_BASE = 0x0000000000401000ull;
    constexpr uint64_t USER_STACK_TOP = 0x0000000000800000ull;
    constexpr uint64_t USER_STACK_PAGES = 1;
    constexpr uint64_t USER_STACK_GUARD_PAGES = 1;
    constexpr uint64_t USER_STACK_GROWTH_PAGES = 4;
    constexpr uint64_t USER_HEAP_MAX_PAGES = 16;
    constexpr uint64_t USER_ANON_BASE = 0x0000000001000000ull;
    constexpr uint64_t USER_ANON_MAX_PAGES = 32;
    constexpr const char *USER_ELF_SMOKE_PATH = "/boot/user/init.elf";
    // Default-on init image path. Semantically neutral name for the normal-boot
    // launch_init path; intentionally the same value as USER_ELF_SMOKE_PATH so
    // the default init and the user_elf_smoke share one packaged artifact.
    constexpr const char *INIT_ELF_PATH = "/boot/user/init.elf";
    constexpr uint64_t USER_ELF_MAX_FILE_BYTES = 64 * 1024;
    constexpr uint64_t USER_LOW_HALF_LIMIT = 0x0000800000000000ull;
    // Growable process/fd tables: the former compile-time hard caps
    // (MAX_PROCESSES = 16, MAX_FDS = 16) are replaced by configurable soft
    // limits. The process registry is an intrusive list and each fd table is a
    // heap-allocated growable array, both bounded by these soft limits rather
    // than a fixed inline size.
    constexpr uint32_t MAX_PROCESSES_SOFT_LIMIT = 1024;
    constexpr uint32_t MAX_FDS_SOFT_LIMIT = 256;
    constexpr uint32_t MAX_VMAS = 16;
    constexpr uint32_t ROOT_PARENT_PID = 0;
    constexpr uint32_t WAIT_ANY = 0xffffffffu;
    constexpr uint32_t EXEC_MAX_ARGC = 8;
    constexpr uint32_t EXEC_MAX_ENVC = 8;
    constexpr uint64_t EXEC_MAX_STRING_BYTES = 256;
    // POSIX-style wait/fd error codes resolve to bigos/errno.h; the negated
    // value (e.g. -bigos::ECHILD) is what reaches the syscall return register.
    // EXEC_FAILURE_STATUS is a process exit status, not a POSIX errno, so it is
    // intentionally kept here and out of the errno convergence scope.
    constexpr int64_t EXEC_FAILURE_STATUS = -126;

    struct FdEntry {
        bigos::vfs::File *file;
        bool close_on_exec;
        bool readable;
    };

    enum class ProcessState : uint8_t {
        Empty = 0,
        Created,
        Running,
        Terminated,
        Faulted,
        Zombie,
        ReapPending,
        Reaped,
    };

    struct UserRange {
        uint64_t base;
        uint64_t len;
    };

    enum class VmaPermission : uint8_t {
        None = 0,
        Read = 1u << 0,
        Write = 1u << 1,
        Execute = 1u << 2,
    };

    enum class VmaPurpose : uint8_t {
        Code = 0,
        Data,
        Heap,
        Anonymous,
        Stack,
        StackGuard,
    };

    enum class VmaBacking : uint8_t {
        Anonymous = 0,
        ElfSegment,
        Guard,
    };

    enum class VmaGrowth : uint8_t {
        None = 0,
        Up,
        Down,
    };

    struct VmaEntry {
        uint64_t start;
        uint64_t end;
        uint64_t materialized_start;
        uint64_t materialized_end;
        VmaPermission permissions;
        VmaPurpose purpose;
        VmaBacking backing;
        VmaGrowth growth;
        bool used;
    };

    struct VmaCollection {
        VmaEntry entries[MAX_VMAS];
        uint32_t count;
        uint64_t heap_base;
        uint64_t heap_break;
        uint64_t heap_limit;
        uint64_t anon_next;
    };

    struct Process {
        uint32_t pid;
        uint32_t parent_pid;
        uint32_t first_child_pid;
        uint32_t next_sibling_pid;
        uint32_t next_reap_pid;
        uint64_t address_space_root;
        uint64_t kernel_address_space_root;
        uint64_t entry;
        UserRange code;
        UserRange data;
        UserRange stack;
        VmaCollection vmas;
        uint64_t initial_stack;
        uint64_t code_phys;
        uint64_t data_phys;
        uint64_t stack_phys;
        void *kernel_stack_base;
        uint64_t kernel_stack_len;
        uint64_t kernel_stack_top;
        ProcessState state;
        bool reap_pending;
        bool resources_reclaimed;
        bool table_published;
        bool wait_status_consumed;
        bool parent_waiting;
        int64_t exit_code;
        int64_t fault_reason;
        // Minimal process identity quad (appended fields; do not reorder the
        // earlier layout). All zero means root. init and non-fork ELF creation
        // initialize these to 0 (root); fork inherits them field-by-field from
        // the parent; exec leaves them unchanged (no setuid bit this stage).
        uint32_t uid;
        uint32_t gid;
        uint32_t euid;
        uint32_t egid;
        // Process creation wall-clock timestamp (Unix epoch seconds) taken at
        // init/ELF/fork creation time. exec does NOT refresh it (exec is not a
        // new process). Signed per the wall-clock API convention.
        int64_t start_unix_time;
        // Growable per-process fd table: heap-allocated FdEntry array bounded by
        // MAX_FDS_SOFT_LIMIT instead of a fixed inline array. Allocated lazily on
        // first install and freed when the process is reaped. fd_capacity is the
        // current allocated entry count; the lowest free slot is still preferred.
        FdEntry *fd_table;
        uint32_t fd_capacity;
        // True when the Process object itself was allocated from the kernel heap
        // via alloc_process_object() and must be freed on reap. Static smoke
        // objects keep this false so the reaper never frees static storage.
        bool heap_allocated;
        // Intrusive process-registry node pointers. The registry is a global
        // doubly linked list threaded through every published Process object;
        // nodes are allocated/freed with the Process itself (no extra node heap
        // allocation). Only valid while table_published is true.
        Process *reg_next;
        Process *reg_prev;
        // Set for a process created by fork(): holds the saved user InterruptFrame
        // (a verbatim copy of the parent's syscall frame with rax rewritten to 0)
        // that the child's first scheduling restores through enter_user_mode_frame.
        // fork_entry_valid gates that path; non-fork processes keep it false and
        // enter ring3 through the ordinary entry/initial-stack path.
        bool fork_entry_valid;
        bigos::irq::InterruptFrame fork_entry_frame;
        // Minimal signal state (appended fields; do not reorder the earlier
        // layout). All inline and fixed-size, so signal delivery and query paths
        // never allocate. sig_pending is the per-signal pending bitmap, sig_mask
        // the blocked mask (bit 1ull << (signo - 1)), and sig_disp the per-signal
        // disposition table indexed by signo - 1. init/non-fork ELF creation
        // zero these to all-default/empty; fork inherits sig_disp and sig_mask
        // field-by-field and clears the child sig_pending; exec resets user
        // handlers to default while preserving sig_mask and sig_pending.
        bigos::signal::SigSet sig_pending;
        bigos::signal::SigSet sig_mask;
        bigos::signal::SigDisposition sig_disp[bigos::signal::SIG_COUNT];
    };

    struct ExecArgs {
        const char *const *argv;
        uint32_t argc;
        const char *const *envp;
        uint32_t envc;
    };

    enum class UserElfLoadError : uint8_t {
        Success = 0,
        InvalidArgument,
        UnsupportedElf,
        BadHeader,
        BadProgramHeader,
        AddressOutOfRange,
        SegmentOverlap,
        UnsafePermissions,
        EntryNotExecutable,
        OutOfMemory,
        MapFailed,
        CopyFailed,
    };

    bool create_first_user_process(Process *__process) noexcept;
    const char *user_elf_load_error_name(UserElfLoadError __error) noexcept;
    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len) noexcept;
    UserElfLoadError create_elf_user_process(
        Process *__process, const void *__image, uint64_t __image_len, const ExecArgs *__args) noexcept;
    UserElfLoadError exec_current_from_elf_image(
        const void *__image, uint64_t __image_len, const ExecArgs *__args) noexcept;
    // SYS_EXECVE backing path: opens __path through the read-only VFS, reads a
    // bounded ELF image into a kernel buffer, replaces the current process image
    // via exec_current_from_elf_image, and enters the new program (does not
    // return on success). On failure it returns a deterministic negative errno
    // (-ENOENT/-EACCES/-ENOEXEC/-E2BIG/-EFAULT/-ENOMEM/-EWOULDBLOCK/-EIO) with
    // the calling process left able to continue. Blockable (CPL3 syscall)
    // context only; checks the scheduler blocking guard before block IO/alloc.
    int64_t execve_current(const char *__path, const ExecArgs *__args) noexcept;
    [[noreturn]] void run_user_process(Process *__process) noexcept;
    Process *current_process() noexcept;
    // Looks up a published, live process by PID for the signal/kill path. Returns
    // nullptr when no active process owns the PID (SYS_KILL maps that to -ESRCH).
    Process *find_process(uint32_t __pid) noexcept;
    void init() noexcept;
    // Default-on, no-#ifdef normal-boot init launch. Loads INIT_ELF_PATH through
    // the read-only VFS/exFAT path and enters ring3; missing/invalid init halts
    // via the unified panic path (PID-1 semantics prototype). Intended to run as
    // a kernel thread created between proc::init() and sched::start().
    void launch_init(void *) noexcept;
    void init_vmas(VmaCollection *__vmas) noexcept;
    bool add_vma(VmaCollection *__vmas, const VmaEntry &__entry) noexcept;
    bool remove_vma(VmaCollection *__vmas, uint64_t __start, uint64_t __end) noexcept;
    const VmaEntry *find_vma(const VmaCollection *__vmas, uint64_t __addr) noexcept;
    bool vma_range_allowed(const VmaCollection *__vmas, uint64_t __addr, uint64_t __len, VmaPermission __perm) noexcept;
    bool vma_attr_allowed(const VmaEntry *__vma, bigos::mm::PageAttr __attr) noexcept;
    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept;
    bool validate_user_io_buffer(uint64_t __addr, uint64_t __len) noexcept;
    bool copy_current_user_buffer(uint64_t __addr, void *__dst, uint64_t __len) noexcept;
    bool copy_to_current_user_buffer(uint64_t __addr, const void *__src, uint64_t __len) noexcept;
    int64_t brk_current(uint64_t __new_break) noexcept;
    int64_t map_anonymous_current(uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept;
    bool try_handle_user_page_fault(uint64_t __fault_address, uint64_t __error_code) noexcept;
    // Duplicates the current user process into a new child (POSIX-style fork).
    // __parent_frame is the parent's saved int 0x80 InterruptFrame from the
    // syscall dispatcher; it is copied verbatim into the child with rax rewritten
    // to 0 so the child resumes from the same instruction returning 0. The child
    // receives a copy-on-write copy of the parent address space (writable
    // anonymous pages shared read-only with the COW marker, ELF segments copied
    // into independent frames), a copied per-process fd table, an independent PID
    // and kernel stack. Returns the child PID to the parent. On any allocation
    // failure it rolls back all partial child state and returns a deterministic
    // negative errno (e.g. -bigos::ENOMEM / -bigos::EAGAIN) with the parent left
    // Running. Non-IRQ / allocation-permitted (CPL3 syscall) context only.
    int64_t fork_current(const bigos::irq::InterruptFrame *__parent_frame) noexcept;
    int64_t install_fd_current(bigos::vfs::File *__file, bool __close_on_exec = false) noexcept;
    bigos::vfs::Status read_fd_current(uint32_t __fd, void *__dst, size_t __len, size_t *__bytes_read) noexcept;
    // Writes through a process-local fd to a writable file or pipe write end.
    bigos::vfs::Status write_fd_current(
        uint32_t __fd, const void *__src, size_t __len, size_t *__bytes_written) noexcept;
    bigos::vfs::Status lseek_fd_current(uint32_t __fd, int64_t __offset, int __whence, uint64_t *__new_offset) noexcept;
    bigos::vfs::Status fsync_fd_current(uint32_t __fd) noexcept;
    bigos::vfs::Status readdir_fd_current(
        uint32_t __fd, bigos::vfs::DirectoryEntry *__entries, size_t __max_entries, size_t *__entries_read) noexcept;
    // Returns the vfs::File bound to a current-process fd, or nullptr on an
    // invalid/unused descriptor. The reference count is not changed.
    bigos::vfs::File *file_for_fd_current(uint32_t __fd) noexcept;
    // dup/dup2 share the underlying vfs::File (offset and ref count). dup picks
    // the lowest free fd; dup2 closes an already-open newfd first then binds it.
    int64_t dup_fd_current(uint32_t __oldfd) noexcept;
    int64_t dup2_fd_current(uint32_t __oldfd, uint32_t __newfd) noexcept;
    int64_t close_fd_current(uint32_t __fd) noexcept;
    void close_all_fds(Process *__process) noexcept;
    void close_on_exec_fds(Process *__process) noexcept;
    int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept;
    // Re-establishes the global ring3 context (current process, user CR3, TSS
    // rsp0) for __process after a blocking syscall may have switched to and from
    // another user kernel thread. Called at the syscall dispatch return boundary.
    void restore_current_user_context(Process *__process) noexcept;
    // Prepares the active address-space context for a scheduler switch to
    // __next_process. User threads need their user CR3 before their kernel stack
    // is loaded; kernel-only threads need the saved kernel CR3 instead.
    void prepare_context_switch_to(Process *__next_process) noexcept;
    void mark_current_faulted(int64_t __reason) noexcept;
    [[noreturn]] void fault_current_and_exit(int64_t __reason) noexcept;
    [[noreturn]] void exit_current(int64_t __code) noexcept;
    void reap_pending_processes() noexcept;

#ifdef BIGOS_USER_PROGRAM_SMOKE
    void user_program_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_USER_ELF_SMOKE
    void user_elf_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_DEMAND_PAGING_SMOKE
    void demand_paging_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_GROWABLE_TABLES_SMOKE
    void growable_tables_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_FORK_COW_SMOKE
    void fork_cow_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_TIME_IDENTITY_SMOKE
    void time_identity_smoke_entry(void *) noexcept;
#endif
#ifdef BIGOS_SIGNAL_SMOKE
    void signal_smoke_entry(void *) noexcept;
#endif
}   // namespace bigos::proc

#endif   // _BIG_PROC_H
