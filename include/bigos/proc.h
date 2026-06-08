#ifndef _BIG_PROC_H
#define _BIG_PROC_H

#include <bigos/types.h>
#include <bigos/fs/vfs.h>
#include <bigos/memory.h>

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
    constexpr uint64_t USER_ELF_MAX_FILE_BYTES = 64 * 1024;
    constexpr uint64_t USER_LOW_HALF_LIMIT = 0x0000800000000000ull;
    constexpr uint32_t MAX_PROCESSES = 16;
    constexpr uint32_t MAX_VMAS = 16;
    constexpr uint32_t ROOT_PARENT_PID = 0;
    constexpr uint32_t WAIT_ANY = 0xffffffffu;
    constexpr uint32_t EXEC_MAX_ARGC = 8;
    constexpr uint32_t EXEC_MAX_ENVC = 8;
    constexpr uint64_t EXEC_MAX_STRING_BYTES = 256;
    constexpr int64_t WAIT_ECHILD = -10;
    constexpr int64_t WAIT_EINVAL = -22;
    constexpr int64_t WAIT_EWOULDBLOCK = -11;
    constexpr int64_t EXEC_FAILURE_STATUS = -126;
    constexpr uint32_t MAX_FDS = 16;
    constexpr int64_t FD_EBADF = -9;
    constexpr int64_t FD_EMFILE = -24;
    constexpr int64_t FD_EWOULDBLOCK = -11;

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
        FdEntry fd_table[MAX_FDS];
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
    [[noreturn]] void run_user_process(Process *__process) noexcept;
    Process *current_process() noexcept;
    void init() noexcept;
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
    bool try_handle_current_stack_fault(uint64_t __fault_address, uint64_t __error_code) noexcept;
    int64_t install_fd_current(bigos::vfs::File *__file, bool __close_on_exec = false) noexcept;
    bigos::vfs::Status read_fd_current(uint32_t __fd, void *__dst, size_t __len, size_t *__bytes_read) noexcept;
    int64_t close_fd_current(uint32_t __fd) noexcept;
    void close_all_fds(Process *__process) noexcept;
    void close_on_exec_fds(Process *__process) noexcept;
    int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept;
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
}   // namespace bigos::proc

#endif   // _BIG_PROC_H
