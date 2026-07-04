#ifndef _BIGOS_FS_BIGFS_H
#define _BIGOS_FS_BIGFS_H

#include <bigos/types.h>
#include <drivers/block/block_device.h>

NAMESPACE_BIGOS_BEG
namespace bigfs {
    // Minimal writable filesystem for /rw. By default it is RAM-backed and
    // current-session-only; BIGOS_PERSISTENT_WRITABLE_FS can instead mount a
    // compatible independent persistent test disk. Layout (in BLOCK_SIZE blocks):
    // superblock, inode bitmap, data bitmap, inode table, fixed journal, then
    // the data region. Everything is read/written through the block buffer cache
    // so writes are write-back and fsync/eviction stay consistent.
    constexpr const char *MOUNT_PREFIX = "/rw";
    constexpr uint32_t MAGIC = 0x42494746;   // "BIGF"
    constexpr uint32_t FORMAT_VERSION = 3;
    constexpr uint32_t BLOCK_SIZE = driver::block::DEFAULT_SECTOR_SIZE;   // 512
    constexpr uint32_t TOTAL_BLOCKS = 256;                                // 128 KiB RAM disk
    constexpr uint32_t INODE_COUNT = 32;
    constexpr uint32_t INODE_SIZE = 128;
    constexpr uint32_t INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;        // 4
    constexpr uint32_t INODE_TABLE_START = 3;                             // blocks 0,1,2 reserved
    constexpr uint32_t INODE_TABLE_BLOCKS = INODE_COUNT / INODES_PER_BLOCK;   // 8
    constexpr uint32_t JOURNAL_START = INODE_TABLE_START + INODE_TABLE_BLOCKS;   // 11
    constexpr uint32_t JOURNAL_BLOCKS = 32;
    constexpr uint32_t JOURNAL_DESCRIPTOR_BLOCK = JOURNAL_START;
    constexpr uint32_t JOURNAL_COMMIT_BLOCK = JOURNAL_START + 1;
    constexpr uint32_t JOURNAL_CHECKPOINT_BLOCK = JOURNAL_START + 2;
    constexpr uint32_t JOURNAL_PAYLOAD_START = JOURNAL_START + 3;
    constexpr uint32_t JOURNAL_PAYLOAD_BLOCKS = JOURNAL_BLOCKS - 3;          // 29
    constexpr uint32_t JOURNAL_MAX_RECORDS = 24;
    constexpr uint32_t DATA_START = JOURNAL_START + JOURNAL_BLOCKS;          // 43
    constexpr uint32_t DATA_BLOCK_COUNT = TOTAL_BLOCKS - DATA_START;          // 213
    constexpr uint32_t DIRECT_BLOCKS = 8;
    constexpr uint64_t MAX_FILE_SIZE = (uint64_t)DIRECT_BLOCKS * BLOCK_SIZE;   // 4096
    constexpr uint32_t DIRENT_SIZE = 32;
    constexpr uint32_t DIRENT_NAME_MAX = 27;   // 28-byte name field, NUL-terminated
    constexpr uint32_t ROOT_INODE = 0;
    constexpr uint32_t INODE_FREE = 0;
    constexpr uint32_t INODE_REGULAR = 1;
    constexpr uint32_t INODE_DIRECTORY = 2;

    struct DirectoryEntry {
        uint32_t type;
        char name[DIRENT_NAME_MAX + 1];
    };

    enum class Status : int32_t {
        Success = 0,
        Invalid,
        NotFound,
        Exists,
        NoSpace,
        NotDirectory,
        IsDirectory,
        NotEmpty,
        AccessDenied,
        IoError,
        Unsupported,
        WouldBlock,
    };

    // Allocates the RAM-backed device, formats a fresh filesystem and prepares
    // the block buffer cache. Idempotent; only the first call allocates. Returns
    // false on allocation failure (no writable mount is published). Blockable /
    // non-IRQ context only.
    bool init() noexcept;
    // Explicitly formats the configured persistent test disk and publishes it as
    // /rw only after initial metadata writes and verification succeed. This is a
    // bounded BigOS-specific mkfs hook, not POSIX device management.
    Status format_persistent() noexcept;
    bool initialized() noexcept;
    bool persistent() noexcept;
    const char *backend_name() noexcept;

    // The backing block device (for smoke-driven cache eviction). Null before init.
    driver::block::BlockDevice *device() noexcept;

    // True when an absolute path targets this writable mount (prefix MOUNT_PREFIX).
    bool owns_path(const char *__abs_path) noexcept;

    // Opens/creates a regular file. __flags uses the vfs OPEN_* bits. On O_CREAT
    // the new file records (__uid, __gid) as owner and __mode as its permission
    // bits; access checks use (__uid, __gid) as the requester identity. On
    // success returns Success and fills __out_inode/__out_size.
    Status open(const char *__abs_path, uint64_t __flags, uint32_t __mode, uint32_t __uid, uint32_t __gid,
        uint32_t *__out_inode, uint64_t *__out_size, bool *__out_is_dir) noexcept;
    void close_inode(uint32_t __inode) noexcept;

    Status read(uint32_t __inode, uint64_t __offset, void *__dst, size_t __len, size_t *__out_read) noexcept;
    Status write(uint32_t __inode, uint64_t __offset, const void *__src, size_t __len, uint32_t __uid, uint32_t __gid,
        size_t *__out_written) noexcept;
    Status truncate(uint32_t __inode, uint64_t __length, uint32_t __uid, uint32_t __gid) noexcept;
    Status utimens(
        const char *__abs_path, uint64_t __atime, uint64_t __mtime, uint32_t __flags, uint32_t __uid,
        uint32_t __gid) noexcept;

    Status mkdir(const char *__abs_path, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept;
    Status unlink(const char *__abs_path, uint32_t __uid, uint32_t __gid) noexcept;
    Status rmdir(const char *__abs_path, uint32_t __uid, uint32_t __gid) noexcept;
    Status rename(const char *__old_abs_path, const char *__new_abs_path, uint32_t __uid, uint32_t __gid) noexcept;
    Status readdir(uint32_t __inode, uint64_t __offset, DirectoryEntry *__entries, size_t __max_entries,
        size_t *__out_entries, uint64_t *__next_offset) noexcept;

    // Flushes pending metadata and every dirty cached block for the writable
    // device. Persistent mounts use a journal-first ordered write path and run
    // bounded mount-time recovery before publishing a writable /rw. This is not
    // a complete POSIX power-loss contract, fdatasync, or async write-back.
    Status fsync() noexcept;

    bool stat(uint32_t __inode, uint32_t *__mode, uint32_t *__uid, uint32_t *__gid, uint64_t *__size,
        bool *__is_dir, uint64_t *__atime, uint64_t *__mtime, uint64_t *__ctime) noexcept;

#ifdef BIGOS_JOURNAL_RECOVERY_SMOKE
    enum class JournalRecoveryTestState : uint32_t {
        Clean = 0,
        Partial = 1,
        Committed = 2,
        Corrupt = 3,
        ReplayInterrupted = 4,
        RecoveryIoFailure = 5,
    };

    Status prepare_journal_recovery_test_state(JournalRecoveryTestState __state) noexcept;
#endif
}   // namespace bigfs
NAMESPACE_BIGOS_END

#endif   // _BIGOS_FS_BIGFS_H
