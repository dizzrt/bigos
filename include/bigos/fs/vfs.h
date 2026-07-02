#ifndef _BIGOS_FS_VFS_H
#define _BIGOS_FS_VFS_H

#include <bigos/metadata.h>
#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace sched {
    // Forward declaration only; the readiness wait-queue op returns opaque
    // scheduler wait-queue handles. vfs.h must not depend on the scheduler
    // layout, so it only names the type here.
    struct WaitQueue;
}   // namespace sched
namespace vfs {
    constexpr uint64_t OPEN_RDONLY = 0;
    constexpr uint64_t OPEN_WRONLY = 1ull << 0;
    constexpr uint64_t OPEN_RDWR = 1ull << 1;
    constexpr uint64_t OPEN_CREAT = 1ull << 6;
    constexpr uint64_t OPEN_TRUNC = 1ull << 9;
    // OPEN_ACCMODE masks the access-mode bits synthesized for F_GETFL.
    // OPEN_NONBLOCK is the bounded nonblocking status flag and the single
    // kernel-internal source for O_NONBLOCK; fd-control (proc::O_NONBLOCK) and
    // the user libc mirror reuse this value. It is the common 1<<11 bit and does
    // not collide with the OPEN_* access/creation flags above.
    constexpr uint64_t OPEN_ACCMODE = OPEN_WRONLY | OPEN_RDWR;
    constexpr uint64_t OPEN_NONBLOCK = 1ull << 11;
    constexpr uint32_t UTIME_ATIME_NOW = 1u << 0;
    constexpr uint32_t UTIME_MTIME_NOW = 1u << 1;
    constexpr uint32_t UTIME_ATIME_OMIT = 1u << 2;
    constexpr uint32_t UTIME_MTIME_OMIT = 1u << 3;
    constexpr uint32_t UTIME_SUPPORTED_FLAGS =
        UTIME_ATIME_NOW | UTIME_MTIME_NOW | UTIME_ATIME_OMIT | UTIME_MTIME_OMIT;
    constexpr size_t MAX_PATH_LEN = 256;
    constexpr size_t DIRENT_NAME_MAX = 27;

    // Kernel-internal fd readiness bit flags returned by poll_file(). They are a
    // kernel-only convention combined with bitwise OR; they are NOT a user-visible
    // syscall ABI. A user-visible multiplexing encoding (POSIX poll-style event
    // bits) is defined separately later and converted at a single point.
    constexpr uint32_t READY_READABLE = 1u << 0;
    constexpr uint32_t READY_WRITABLE = 1u << 1;
    constexpr uint32_t READY_ERROR = 1u << 2;

    enum class Status : int64_t {
        Success = 0,
        InvalidArgument = -22,
        BadFileDescriptor = -9,
        NotInitialized = -19,
        NoMemory = -12,
        NotFound = -2,
        NotRegularFile = -21,
        Unsupported = -95,
        Overflow = -75,
        BlockError = -5,
        WouldBlock = -11,
        ReadOnlyFs = -30,
        NoSpace = -28,
        AccessDenied = -13,
        NotDirectory = -20,
        NotSeekable = -29,
        IsDirectory = -21,
        Exists = -17,
        BrokenPipe = -32,
        Range = -34,
        NotEmpty = -39,
        // Appended connection-oriented statuses for stream (TCP) sockets. Their
        // numeric values are the negated POSIX errno the dispatcher writes back
        // (ECONNRESET=104, ENOTCONN=107), so a stream socket read/write routed
        // through the ops table surfaces the right errno without a separate map.
        ConnectionReset = -104,
        NotConnected = -107,
    };

    // Seek whence values for lseek (POSIX numeric layout).
    constexpr int SEEK_SET = 0;
    constexpr int SEEK_CUR = 1;
    constexpr int SEEK_END = 2;

    struct Vnode;
    struct File;
    struct DirectoryEntry {
        uint32_t type;
        char name[DIRENT_NAME_MAX + 1];
    };

    constexpr uint32_t DIRENT_TYPE_FILE = 1;
    constexpr uint32_t DIRENT_TYPE_DIRECTORY = 2;

    using ReadOp = Status (*)(File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept;
    using WriteOp = Status (*)(File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept;
    using LseekOp = Status (*)(File *__file, int64_t __offset, int __whence, uint64_t *__new_offset) noexcept;
    using TruncateOp = Status (*)(File *__file, uint64_t __length) noexcept;
    using ReaddirOp = Status (*)(
        File *__file, DirectoryEntry *__entries, size_t __max_entries, size_t *__entries_read) noexcept;
    using CloseOp = void (*)(File *__file) noexcept;
    // Readiness snapshot op. Returns the bitwise OR of READY_* flags for the
    // file. It MUST be a pure read-only snapshot: no dequeue, no blocking, no
    // change to the file or backend open state.
    using PollOp = uint32_t (*)(File *__file) noexcept;
    // Readiness wait-queue collection op. Writes up to __max scheduler wait-queue
    // handles into __out that a multiplexing wait should register on for the
    // requested __events, and returns the number written (0..__max). It MUST be
    // read-only: it returns queue handles only, dequeues nothing, and changes no
    // backend state. A backend that leaves this null contributes no wait queues
    // (e.g. a regular file that poll_file() reports as always ready).
    using PollWaitOp = uint32_t (*)(File *__file, uint32_t __events, sched::WaitQueue **__out, uint32_t __max) noexcept;

    struct FileOperations {
        ReadOp read;
        CloseOp close;
        // Appended ops (do not reorder read/close above). A read-only backend may
        // leave write null (the wrapper then returns ReadOnlyFs) and lseek null
        // (the wrapper then performs ordinary offset arithmetic).
        WriteOp write;
        LseekOp lseek;
        TruncateOp truncate;
        ReaddirOp readdir;
        // Appended readiness op (do not reorder the slots above). A backend that
        // leaves this null gets a deterministic readable+writable default from
        // poll_file().
        PollOp poll;
        // Appended readiness wait-queue op (do not reorder the slots above). A
        // backend that leaves this null contributes no wait queues to a
        // multiplexing wait.
        PollWaitOp poll_wait;
    };

    // Layout guard: the appended poll op MUST stay after the existing slots so
    // statically-initialized backend ops tables keep their meaning.
    static_assert(__builtin_offsetof(FileOperations, read) == 0, "read slot moved");
    static_assert(__builtin_offsetof(FileOperations, close) == sizeof(void *), "close slot moved");
    static_assert(__builtin_offsetof(FileOperations, write) == 2 * sizeof(void *), "write slot moved");
    static_assert(__builtin_offsetof(FileOperations, lseek) == 3 * sizeof(void *), "lseek slot moved");
    static_assert(__builtin_offsetof(FileOperations, truncate) == 4 * sizeof(void *), "truncate slot moved");
    static_assert(__builtin_offsetof(FileOperations, readdir) == 5 * sizeof(void *), "readdir slot moved");
    static_assert(__builtin_offsetof(FileOperations, poll) == 6 * sizeof(void *), "poll slot moved");
    static_assert(
        __builtin_offsetof(FileOperations, poll_wait) == 7 * sizeof(void *), "poll_wait slot must be last");

    struct Vnode {
        uint64_t size;
        bool is_directory;
        void *private_data;
    };

    struct FileIdentity {
        uint32_t backend_id;
        uint32_t mount_id;
        uint64_t object_id;
    };

    constexpr uint32_t FILE_BACKEND_NONE = 0;
    constexpr uint32_t FILE_BACKEND_EXFAT = 1;
    constexpr uint32_t FILE_BACKEND_BIGFS = 2;

    struct File {
        const FileOperations *ops;
        Vnode *vnode;
        uint64_t offset;
        uint32_t ref_count;
        bool readable;
        bool close_on_exec;
        void *private_data;
        // Appended field (do not reorder the layout above). True when the file
        // object was opened with write access.
        bool writable;
        FileIdentity identity;
        // Appended field (do not reorder the layout above). Bounded O_NONBLOCK
        // status flag at open-file-description granularity, default false. It is
        // shared through dup/fork because those paths share the same File object
        // (vfs::retain), matching POSIX "the flag belongs to the open file
        // description". Backends consult file_is_nonblocking() at their
        // would-block decision points.
        bool nonblocking;
    };

    // Layout guard: the nonblocking flag MUST stay appended after identity (and
    // identity after writable) so the earlier fields keep their offsets for
    // backends and statically-built smoke state.
    static_assert(__builtin_offsetof(File, ops) == 0, "File ops slot moved");
    static_assert(__builtin_offsetof(File, identity) > __builtin_offsetof(File, writable),
        "File identity must stay after writable");
    static_assert(__builtin_offsetof(File, nonblocking) > __builtin_offsetof(File, identity),
        "File nonblocking must be appended after identity");

    // Kernel-internal helpers shared by fd-control and the would-block decision
    // points in each blockable backend.
    //
    // file_is_nonblocking reports the open file description's bounded O_NONBLOCK
    // flag; a null file reports false (callers fall back to the !can_block()
    // short-circuit). file_access_mode synthesizes the OPEN_RDONLY/WRONLY/RDWR
    // access bits from the file's readable/writable state for F_GETFL.
    inline bool file_is_nonblocking(const File *__file) noexcept {
        return __file != nullptr && __file->nonblocking;
    }

    inline uint64_t file_access_mode(const File *__file) noexcept {
        if (__file == nullptr)
            return OPEN_RDONLY;
        if (__file->readable && __file->writable)
            return OPEN_RDWR;
        if (__file->writable)
            return OPEN_WRONLY;
        return OPEN_RDONLY;
    }

    const char *status_name(Status __status) noexcept;
    Status init() noexcept;
    bool initialized() noexcept;
    // Normalizes absolute paths from root and relative paths from __cwd into a
    // bounded absolute path. Supports POSIX-style "." and ".." components only;
    // root's parent remains root and empty/overlong paths fail deterministically.
    Status resolve_path(const char *__path, const char *__cwd, char *__out, size_t __out_len) noexcept;
    Status open_absolute(const char *__path, uint64_t __flags, File **__out_file) noexcept;
    // Writable/creating open. __mode/__uid/__gid apply to O_CREAT; for read-only
    // opens they are ignored. The simpler open_absolute() forwards here as a
    // read-only request preserving its existing semantics.
    Status open_absolute(const char *__path, uint64_t __flags, uint32_t __mode, uint32_t __uid, uint32_t __gid,
        File **__out_file) noexcept;
    Status open(const char *__path, const char *__cwd, uint64_t __flags, uint32_t __mode, uint32_t __uid,
        uint32_t __gid, File **__out_file) noexcept;
    Status read(File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept;
    // Positioned read for file-backed page materialization. Reads up to __len
    // bytes at the absolute file offset __offset through the existing cache-backed
    // read path without permanently changing __file->offset. It is the single
    // page-granular entry the demand-paging file-backed branch uses, so a single
    // page that spans multiple cache blocks is aggregated by the backend read
    // path and any underlying block IO error fails the whole call deterministically.
    Status pread(File *__file, uint64_t __offset, void *__dst, size_t __len, size_t *__bytes_read) noexcept;
    Status write(File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept;
    Status lseek(File *__file, int64_t __offset, int __whence, uint64_t *__new_offset) noexcept;
    Status truncate(File *__file, uint64_t __length) noexcept;
    Status fsync(File *__file) noexcept;
    // Bounded explicit synchronization for BigOS's active writable backend.
    // Flushes pending backend metadata and device-scoped cache dirty state only;
    // this is not complete POSIX sync(2), fdatasync, async write-back, mount
    // namespace synchronization, journaling, or crash/power-loss recovery.
    Status sync_writable_backend() noexcept;
    Status readdir(File *__file, DirectoryEntry *__entries, size_t __max_entries, size_t *__entries_read) noexcept;
    // Unified kernel-internal fd readiness snapshot. Dispatches to ops->poll when
    // present; otherwise returns the deterministic default READY_READABLE |
    // READY_WRITABLE (no error). It is a pure read-only query: it never dequeues
    // data, blocks, or changes the file/backend open state. A null file returns
    // READY_ERROR. The returned bits are a kernel-internal convention, not a
    // user-visible syscall ABI.
    uint32_t poll_file(File *__file) noexcept;
    // Kernel-internal helper collecting the scheduler wait queues a multiplexing
    // wait should register on for __file given the requested __events. Dispatches
    // to ops->poll_wait when present; a backend without it contributes 0 queues
    // (those files are always ready by poll_file() and need no wait). Writes up to
    // __max handles into __out and returns the count written. Read-only: it never
    // dequeues, blocks, or changes backend state. A null file or null out returns 0.
    uint32_t file_poll_wait_queues(
        File *__file, uint32_t __events, sched::WaitQueue **__out, uint32_t __max) noexcept;
    Status stat_absolute(const char *__path, bigos::Metadata *__out) noexcept;
    Status stat_path(const char *__path, const char *__cwd, bigos::Metadata *__out) noexcept;
    Status stat(File *__file, bigos::Metadata *__out) noexcept;
    bool file_identity(File *__file, FileIdentity *__out) noexcept;
    // Directory mutation on the writable backend. owner/identity are the caller's.
    Status mkdir(const char *__path, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept;
    Status mkdir(const char *__path, const char *__cwd, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept;
    Status unlink(const char *__path, uint32_t __uid, uint32_t __gid) noexcept;
    Status unlink(const char *__path, const char *__cwd, uint32_t __uid, uint32_t __gid) noexcept;
    Status rmdir(const char *__path, uint32_t __uid, uint32_t __gid) noexcept;
    Status rmdir(const char *__path, const char *__cwd, uint32_t __uid, uint32_t __gid) noexcept;
    Status rename(const char *__old_path, const char *__new_path, uint32_t __uid, uint32_t __gid) noexcept;
    Status rename(const char *__old_path, const char *__new_path, const char *__cwd, uint32_t __uid,
        uint32_t __gid) noexcept;
    Status utimens(
        const char *__path, uint64_t __atime, uint64_t __mtime, uint32_t __flags, uint32_t __uid,
        uint32_t __gid) noexcept;
    Status utimens(const char *__path, const char *__cwd, uint64_t __atime, uint64_t __mtime, uint32_t __flags,
        uint32_t __uid, uint32_t __gid) noexcept;
    void retain(File *__file) noexcept;
    void release(File *__file) noexcept;
}   // namespace vfs
NAMESPACE_BIGOS_END

#endif   // _BIGOS_FS_VFS_H
