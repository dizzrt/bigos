#ifndef _BIGOS_FS_EXFAT_H
#define _BIGOS_FS_EXFAT_H

#include <drivers/block/block_device.h>

NAMESPACE_BIGOS_BEG
namespace fs {
    enum class FsStatus : uint32_t {
        Success = 0,
        InvalidArgument,
        BufferTooSmall,
        Overflow,
        OutOfMemory,
        BlockError,
        NoSupportedPartition,
        MalformedFilesystem,
        Unsupported,
        NotFound,
        NotRegularFile,
    };

    struct Partition {
        uint64_t lba;
        uint64_t sector_count;
    };

    struct ExfatMount {
        driver::block::BlockDevice *device;
        Partition partition;
        uint32_t sector_size;
        uint32_t sectors_per_cluster;
        uint32_t bytes_per_cluster;
        uint64_t fat_lba;
        uint32_t fat_sectors;
        uint64_t cluster_heap_lba;
        uint32_t cluster_count;
        uint32_t root_directory_cluster;
    };

    struct FileMetadata {
        uint32_t first_cluster;
        uint64_t data_length;
        bool is_directory;
        bool no_fat_chain;
    };

    constexpr uint32_t EXFAT_DIRENT_NAME_MAX = 27;
    constexpr uint32_t EXFAT_DIRENT_TYPE_FILE = 1;
    constexpr uint32_t EXFAT_DIRENT_TYPE_DIRECTORY = 2;

    struct DirectoryEntry {
        uint32_t type;
        char name[EXFAT_DIRENT_NAME_MAX + 1];
    };

    struct ReadResult {
        FsStatus status;
        size_t bytes_read;
    };

    const char *status_name(FsStatus __status) noexcept;
    FsStatus find_exfat_partition(driver::block::BlockDevice *__device, Partition *__out) noexcept;
    FsStatus mount_exfat(driver::block::BlockDevice *__device, const Partition *__partition, ExfatMount *__out) noexcept;
    FsStatus lookup(ExfatMount *__mount, const char *__absolute_path, FileMetadata *__out) noexcept;
    ReadResult read_file(
        ExfatMount *__mount, const FileMetadata *__file, uint64_t __offset, void *__dst, size_t __len,
        size_t __dst_len) noexcept;
    FsStatus readdir(ExfatMount *__mount, const FileMetadata *__directory, uint64_t __offset, DirectoryEntry *__entries,
        size_t __max_entries, size_t *__entries_read, uint64_t *__next_offset) noexcept;
}   // namespace fs
NAMESPACE_BIGOS_END

#endif   // _BIGOS_FS_EXFAT_H
