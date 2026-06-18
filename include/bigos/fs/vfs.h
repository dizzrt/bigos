#ifndef _BIGOS_FS_VFS_H
#define _BIGOS_FS_VFS_H

#include <bigos/metadata.h>
#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace vfs {
    constexpr uint64_t OPEN_RDONLY = 0;
    constexpr uint64_t OPEN_WRONLY = 1ull << 0;
    constexpr uint64_t OPEN_RDWR = 1ull << 1;
    constexpr uint64_t OPEN_CREAT = 1ull << 6;
    constexpr uint64_t OPEN_TRUNC = 1ull << 9;
    constexpr size_t MAX_PATH_LEN = 256;
    constexpr size_t DIRENT_NAME_MAX = 27;

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
    };

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
    };

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
    void retain(File *__file) noexcept;
    void release(File *__file) noexcept;
}   // namespace vfs
NAMESPACE_BIGOS_END

#endif   // _BIGOS_FS_VFS_H
