#include <bigos/fs/exfat.h>
#include <bigos/block_io.h>
#include <bigos/memory.h>
#include <string.h>

namespace {
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t EXFAT_PARTITION_TYPE = 0x07;
    constexpr uint32_t MBR_PARTITION_TABLE_OFFSET = 0x1be;
    constexpr uint32_t MBR_PARTITION_ENTRY_SIZE = 16;
    constexpr uint32_t MBR_SIGNATURE_OFFSET = 0x1fe;
    constexpr uint32_t ENTRY_SIZE = 32;
    constexpr uint8_t ENTRY_FILE = 0x85;
    constexpr uint8_t ENTRY_STREAM = 0xc0;
    constexpr uint8_t ENTRY_NAME = 0xc1;
    constexpr uint16_t ATTR_DIRECTORY = 0x10;
    constexpr uint16_t ATTR_ARCHIVE = 0x20;
    constexpr uint8_t STREAM_NO_FAT_CHAIN = 0x02;
    constexpr uint32_t FIRST_DATA_CLUSTER = 2;
    constexpr uint32_t FAT_EOF_START = 0xfffffff8u;
    constexpr uint32_t FAT_BAD_CLUSTER = 0xfffffff7u;
    constexpr uint32_t MAX_COMPONENT_LENGTH = 255;
    constexpr uint32_t MAX_CLUSTER_BYTES = 64 * 1024;

    uint16_t le16(const uint8_t *__p) noexcept {
        return (uint16_t)__p[0] | ((uint16_t)__p[1] << 8);
    }

    uint32_t le32(const uint8_t *__p) noexcept {
        return (uint32_t)__p[0] | ((uint32_t)__p[1] << 8) | ((uint32_t)__p[2] << 16) | ((uint32_t)__p[3] << 24);
    }

    uint64_t le64(const uint8_t *__p) noexcept {
        return (uint64_t)le32(__p) | ((uint64_t)le32(__p + 4) << 32);
    }

    bool add_overflows(uint64_t __a, uint64_t __b, uint64_t *__out) noexcept {
        if (__a > UINT64_MAX - __b)
            return true;
        if (__out != nullptr)
            *__out = __a + __b;
        return false;
    }

    bool mul_overflows(uint64_t __a, uint64_t __b, uint64_t *__out) noexcept {
        if (__a != 0 && __b > UINT64_MAX / __a)
            return true;
        if (__out != nullptr)
            *__out = __a * __b;
        return false;
    }

    bigos::fs::FsStatus block_to_fs(bigos::block_io::Status __status) noexcept {
        return __status == bigos::block_io::Status::Success ? bigos::fs::FsStatus::Success :
                                                              bigos::fs::FsStatus::BlockError;
    }

    bigos::fs::FsStatus read_sector(
        driver::block::BlockDevice *__device, uint64_t __lba, uint8_t *__buffer) noexcept {
        return block_to_fs(bigos::block_io::read_sync(__device, __lba, 1, __buffer, SECTOR_SIZE));
    }

    bool cluster_valid(const bigos::fs::ExfatMount *__mount, uint32_t __cluster) noexcept {
        return __mount != nullptr && __cluster >= FIRST_DATA_CLUSTER &&
               __cluster < FIRST_DATA_CLUSTER + __mount->cluster_count;
    }

    bigos::fs::FsStatus cluster_lba(
        const bigos::fs::ExfatMount *__mount, uint32_t __cluster, uint64_t *__out_lba) noexcept {
        if (!cluster_valid(__mount, __cluster) || __out_lba == nullptr)
            return bigos::fs::FsStatus::MalformedFilesystem;
        uint64_t cluster_index = __cluster - FIRST_DATA_CLUSTER;
        uint64_t offset = 0;
        if (mul_overflows(cluster_index, __mount->sectors_per_cluster, &offset))
            return bigos::fs::FsStatus::Overflow;
        uint64_t lba = 0;
        if (add_overflows(__mount->cluster_heap_lba, offset, &lba))
            return bigos::fs::FsStatus::Overflow;
        const uint64_t partition_end = __mount->partition.lba + __mount->partition.sector_count;
        if (lba < __mount->partition.lba || lba >= partition_end || __mount->sectors_per_cluster > partition_end - lba)
            return bigos::fs::FsStatus::MalformedFilesystem;
        *__out_lba = lba;
        return bigos::fs::FsStatus::Success;
    }

    bigos::fs::FsStatus read_cluster(
        bigos::fs::ExfatMount *__mount, uint32_t __cluster, uint8_t *__buffer) noexcept {
        uint64_t lba = 0;
        bigos::fs::FsStatus status = cluster_lba(__mount, __cluster, &lba);
        if (status != bigos::fs::FsStatus::Success)
            return status;
        return block_to_fs(bigos::block_io::read_sync(
            __mount->device, lba, __mount->sectors_per_cluster, __buffer, __mount->bytes_per_cluster));
    }

    bigos::fs::FsStatus validate_boot_region(
        driver::block::BlockDevice *__device, const bigos::fs::Partition *__partition,
        bigos::fs::ExfatMount *__out) noexcept {
        if (__device == nullptr || __partition == nullptr || __partition->lba == 0 || __partition->sector_count == 0)
            return bigos::fs::FsStatus::InvalidArgument;

        uint8_t sector[SECTOR_SIZE];
        bigos::fs::FsStatus status = read_sector(__device, __partition->lba, sector);
        if (status != bigos::fs::FsStatus::Success)
            return status;
        if (sector[0x1fe] != 0x55 || sector[0x1ff] != 0xaa)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const char exfat_name[] = "EXFAT   ";
        for (uint32_t i = 0; i < 8; i++) {
            if (sector[3 + i] != (uint8_t)exfat_name[i])
                return bigos::fs::FsStatus::MalformedFilesystem;
        }

        const uint8_t bytes_per_sector_shift = sector[0x6c];
        const uint8_t sectors_per_cluster_shift = sector[0x6d];
        if (bytes_per_sector_shift != 9 || sectors_per_cluster_shift > 7)
            return bigos::fs::FsStatus::Unsupported;

        const uint32_t sector_size = 1u << bytes_per_sector_shift;
        const uint32_t sectors_per_cluster = 1u << sectors_per_cluster_shift;
        const uint64_t partition_offset = le64(sector + 0x40);
        const uint64_t volume_length = le64(sector + 0x48);
        const uint32_t fat_offset = le32(sector + 0x50);
        const uint32_t fat_length = le32(sector + 0x54);
        const uint32_t cluster_heap_offset = le32(sector + 0x58);
        const uint32_t cluster_count = le32(sector + 0x5c);
        const uint32_t root_directory_cluster = le32(sector + 0x60);
        const uint8_t fat_count = sector[0x6e];

        if (sector_size != SECTOR_SIZE || sectors_per_cluster == 0 ||
            sector_size * sectors_per_cluster > MAX_CLUSTER_BYTES || fat_count == 0 || fat_offset == 0 ||
            fat_length == 0 || cluster_heap_offset == 0 || cluster_count == 0)
            return bigos::fs::FsStatus::Unsupported;
        if (partition_offset != 0 && partition_offset != __partition->lba)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (volume_length != 0 && volume_length > __partition->sector_count)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (fat_offset > __partition->sector_count || fat_length > __partition->sector_count - fat_offset)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (cluster_heap_offset > __partition->sector_count)
            return bigos::fs::FsStatus::MalformedFilesystem;

        uint64_t heap_span = 0;
        if (mul_overflows(cluster_count, sectors_per_cluster, &heap_span))
            return bigos::fs::FsStatus::Overflow;
        if (heap_span > __partition->sector_count - cluster_heap_offset)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (root_directory_cluster < FIRST_DATA_CLUSTER || root_directory_cluster >= FIRST_DATA_CLUSTER + cluster_count)
            return bigos::fs::FsStatus::MalformedFilesystem;

        if (__out != nullptr) {
            __out->device = __device;
            __out->partition = *__partition;
            __out->sector_size = sector_size;
            __out->sectors_per_cluster = sectors_per_cluster;
            __out->bytes_per_cluster = sector_size * sectors_per_cluster;
            __out->fat_lba = __partition->lba + fat_offset;
            __out->fat_sectors = fat_length;
            __out->cluster_heap_lba = __partition->lba + cluster_heap_offset;
            __out->cluster_count = cluster_count;
            __out->root_directory_cluster = root_directory_cluster;
        }
        return bigos::fs::FsStatus::Success;
    }

    bool ascii_case_equal(uint16_t __utf16, char __ascii) noexcept {
        if (__utf16 > 0x7f)
            return false;
        char a = (char)__utf16;
        char b = __ascii;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        return a == b;
    }

    bool name_matches(const uint16_t *__name, uint32_t __name_len, const char *__component, uint32_t __component_len) noexcept {
        if (__name_len != __component_len)
            return false;
        for (uint32_t i = 0; i < __name_len; i++) {
            if (!ascii_case_equal(__name[i], __component[i]))
                return false;
        }
        return true;
    }

    bigos::fs::FsStatus parse_entry_set(
        const uint8_t *__dir, size_t __dir_len, size_t __offset, const char *__component, uint32_t __component_len,
        bigos::fs::FileMetadata *__out, bool *__matched) noexcept {
        *__matched = false;
        const uint8_t *file = __dir + __offset;
        const uint32_t secondary_count = file[1];
        if (secondary_count < 2)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const size_t set_size = (size_t)(secondary_count + 1) * ENTRY_SIZE;
        if (__offset + set_size > __dir_len)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const uint8_t *stream = file + ENTRY_SIZE;
        if (stream[0] != ENTRY_STREAM)
            return bigos::fs::FsStatus::MalformedFilesystem;

        const uint32_t name_length = stream[3];
        const uint32_t name_entry_count = secondary_count - 1;
        if (name_length > name_entry_count * 15 || name_length > MAX_COMPONENT_LENGTH)
            return bigos::fs::FsStatus::MalformedFilesystem;

        uint16_t name[MAX_COMPONENT_LENGTH];
        uint32_t name_index = 0;
        for (uint32_t entry_index = 0; entry_index < name_entry_count; entry_index++) {
            const uint8_t *name_entry = file + (2 + entry_index) * ENTRY_SIZE;
            if (name_entry[0] != ENTRY_NAME)
                return bigos::fs::FsStatus::MalformedFilesystem;
            for (uint32_t char_index = 0; char_index < 15 && name_index < name_length; char_index++) {
                name[name_index++] = le16(name_entry + 2 + char_index * 2);
            }
        }
        if (name_index != name_length)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (!name_matches(name, name_length, __component, __component_len))
            return bigos::fs::FsStatus::Success;

        const uint16_t attributes = le16(file + 4);
        const bool is_directory = (attributes & ATTR_DIRECTORY) != 0;
        const bool is_file = (attributes & ATTR_ARCHIVE) != 0;
        if (!is_directory && !is_file)
            return bigos::fs::FsStatus::Unsupported;

        const uint64_t valid_length = le64(stream + 8);
        const uint32_t first_cluster = le32(stream + 20);
        const uint64_t data_length = le64(stream + 24);
        if (valid_length > data_length)
            return bigos::fs::FsStatus::MalformedFilesystem;
        if (data_length != 0 && first_cluster < FIRST_DATA_CLUSTER)
            return bigos::fs::FsStatus::MalformedFilesystem;

        __out->first_cluster = first_cluster;
        __out->data_length = data_length;
        __out->is_directory = is_directory;
        __out->no_fat_chain = (stream[1] & STREAM_NO_FAT_CHAIN) != 0;
        *__matched = true;
        return bigos::fs::FsStatus::Success;
    }

    bigos::fs::FsStatus parse_entry_set_for_listing(const uint8_t *__dir, size_t __dir_len, size_t __offset,
        bigos::fs::DirectoryEntry *__out, size_t *__set_size) noexcept {
        if (__out == nullptr || __set_size == nullptr)
            return bigos::fs::FsStatus::InvalidArgument;
        const uint8_t *file = __dir + __offset;
        const uint32_t secondary_count = file[1];
        if (secondary_count < 2)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const size_t set_size = (size_t)(secondary_count + 1) * ENTRY_SIZE;
        if (__offset + set_size > __dir_len)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const uint8_t *stream = file + ENTRY_SIZE;
        if (stream[0] != ENTRY_STREAM)
            return bigos::fs::FsStatus::MalformedFilesystem;

        const uint16_t attributes = le16(file + 4);
        const bool is_directory = (attributes & ATTR_DIRECTORY) != 0;
        const bool is_file = (attributes & ATTR_ARCHIVE) != 0;
        if (!is_directory && !is_file)
            return bigos::fs::FsStatus::Unsupported;

        const uint32_t name_length = stream[3];
        const uint32_t name_entry_count = secondary_count - 1;
        if (name_length > name_entry_count * 15 || name_length > MAX_COMPONENT_LENGTH)
            return bigos::fs::FsStatus::MalformedFilesystem;

        __out->type = is_directory ? bigos::fs::EXFAT_DIRENT_TYPE_DIRECTORY : bigos::fs::EXFAT_DIRENT_TYPE_FILE;
        uint32_t out_index = 0;
        uint32_t name_index = 0;
        for (uint32_t entry_index = 0; entry_index < name_entry_count; entry_index++) {
            const uint8_t *name_entry = file + (2 + entry_index) * ENTRY_SIZE;
            if (name_entry[0] != ENTRY_NAME)
                return bigos::fs::FsStatus::MalformedFilesystem;
            for (uint32_t char_index = 0; char_index < 15 && name_index < name_length; char_index++, name_index++) {
                const uint16_t ch = le16(name_entry + 2 + char_index * 2);
                if (out_index < bigos::fs::EXFAT_DIRENT_NAME_MAX)
                    __out->name[out_index++] = (ch >= 0x20 && ch <= 0x7e) ? (char)ch : '?';
            }
        }
        if (name_index != name_length)
            return bigos::fs::FsStatus::MalformedFilesystem;
        __out->name[out_index] = 0;
        *__set_size = set_size;
        return bigos::fs::FsStatus::Success;
    }

    bigos::fs::FsStatus read_fat_entry(
        bigos::fs::ExfatMount *__mount, uint32_t __cluster, uint32_t *__next_cluster) noexcept {
        if (!cluster_valid(__mount, __cluster) || __next_cluster == nullptr)
            return bigos::fs::FsStatus::MalformedFilesystem;
        const uint64_t fat_offset = (uint64_t)__cluster * sizeof(uint32_t);
        const uint64_t sector_offset = fat_offset / SECTOR_SIZE;
        const uint64_t within_sector = fat_offset % SECTOR_SIZE;
        if (sector_offset >= __mount->fat_sectors || within_sector > SECTOR_SIZE - sizeof(uint32_t))
            return bigos::fs::FsStatus::MalformedFilesystem;

        uint8_t sector[SECTOR_SIZE];
        bigos::fs::FsStatus status = read_sector(__mount->device, __mount->fat_lba + sector_offset, sector);
        if (status != bigos::fs::FsStatus::Success)
            return status;
        const uint32_t value = le32(sector + within_sector);
        if (value == FAT_BAD_CLUSTER || value == 0xffffffffu)
            return bigos::fs::FsStatus::MalformedFilesystem;
        *__next_cluster = value;
        return bigos::fs::FsStatus::Success;
    }

    bigos::fs::FsStatus next_cluster(
        bigos::fs::ExfatMount *__mount, const bigos::fs::FileMetadata *__file, uint32_t __current,
        uint32_t *__next) noexcept {
        if (__file->no_fat_chain) {
            if (__current == UINT32_MAX)
                return bigos::fs::FsStatus::MalformedFilesystem;
            *__next = __current + 1;
            return cluster_valid(__mount, *__next) ? bigos::fs::FsStatus::Success :
                                                     bigos::fs::FsStatus::MalformedFilesystem;
        }

        uint32_t value = 0;
        bigos::fs::FsStatus status = read_fat_entry(__mount, __current, &value);
        if (status != bigos::fs::FsStatus::Success)
            return status;
        if (value >= FAT_EOF_START) {
            *__next = 0;
            return bigos::fs::FsStatus::Success;
        }
        if (!cluster_valid(__mount, value) || value < FIRST_DATA_CLUSTER)
            return bigos::fs::FsStatus::MalformedFilesystem;
        *__next = value;
        return bigos::fs::FsStatus::Success;
    }

    bool mark_chain_visit(uint8_t *__visited, uint32_t __cluster) noexcept {
        if (__visited == nullptr)
            return false;
        const uint32_t index = __cluster - FIRST_DATA_CLUSTER;
        const uint8_t mask = (uint8_t)(1u << (index % 8));
        uint8_t *slot = __visited + index / 8;
        if ((*slot & mask) != 0)
            return true;
        *slot |= mask;
        return false;
    }

    bigos::fs::FsStatus find_child(
        bigos::fs::ExfatMount *__mount, const bigos::fs::FileMetadata *__directory, const char *__component,
        uint32_t __component_len, bigos::fs::FileMetadata *__out) noexcept {
        if (__component_len == 0 || __component_len > MAX_COMPONENT_LENGTH)
            return bigos::fs::FsStatus::InvalidArgument;
        if (!__directory->is_directory || __directory->first_cluster < FIRST_DATA_CLUSTER)
            return bigos::fs::FsStatus::MalformedFilesystem;

        uint8_t *cluster_buffer = (uint8_t *)bigos::kmalloc(__mount->bytes_per_cluster);
        if (cluster_buffer == nullptr)
            return bigos::fs::FsStatus::OutOfMemory;
        const size_t visited_bytes = (__mount->cluster_count + 7) / 8;
        uint8_t *visited = nullptr;
        if (!__directory->no_fat_chain) {
            visited = (uint8_t *)bigos::kmalloc(visited_bytes);
            if (visited == nullptr) {
                bigos::free(cluster_buffer);
                return bigos::fs::FsStatus::OutOfMemory;
            }
            memset(visited, 0, visited_bytes);
        }

        bigos::fs::FsStatus result = bigos::fs::FsStatus::NotFound;
        uint32_t cluster = __directory->first_cluster;
        const uint64_t total_len =
            __directory->data_length == 0 ? __mount->bytes_per_cluster : __directory->data_length;
        const uint64_t max_clusters = (total_len + __mount->bytes_per_cluster - 1) / __mount->bytes_per_cluster;
        for (uint64_t step = 0; step < max_clusters; step++) {
            if (mark_chain_visit(visited, cluster)) {
                result = bigos::fs::FsStatus::MalformedFilesystem;
                break;
            }
            result = read_cluster(__mount, cluster, cluster_buffer);
            if (result != bigos::fs::FsStatus::Success)
                break;

            size_t scan_len = __mount->bytes_per_cluster;
            if ((step + 1) * (uint64_t)__mount->bytes_per_cluster > total_len) {
                scan_len = (size_t)(total_len - step * (uint64_t)__mount->bytes_per_cluster);
            }
            for (size_t offset = 0; offset + ENTRY_SIZE <= scan_len;) {
                const uint8_t entry_type = cluster_buffer[offset];
                if (entry_type == 0) {
                    result = bigos::fs::FsStatus::NotFound;
                    goto done;
                }
                if (entry_type != ENTRY_FILE) {
                    offset += ENTRY_SIZE;
                    continue;
                }
                bool matched = false;
                result = parse_entry_set(cluster_buffer, scan_len, offset, __component, __component_len, __out, &matched);
                if (result != bigos::fs::FsStatus::Success)
                    goto done;
                const uint32_t secondary_count = cluster_buffer[offset + 1];
                offset += (size_t)(secondary_count + 1) * ENTRY_SIZE;
                if (matched) {
                    __out->parent_cluster = cluster;
                    __out->entry_offset = (uint32_t)offset;
                    result = bigos::fs::FsStatus::Success;
                    goto done;
                }
            }

            if (step + 1 == max_clusters)
                break;
            uint32_t next = 0;
            result = next_cluster(__mount, __directory, cluster, &next);
            if (result != bigos::fs::FsStatus::Success)
                break;
            if (next == 0) {
                result = bigos::fs::FsStatus::MalformedFilesystem;
                break;
            }
            cluster = next;
        }

    done:
        if (visited != nullptr)
            bigos::free(visited);
        bigos::free(cluster_buffer);
        return result;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace fs {
    const char *status_name(FsStatus __status) noexcept {
        switch (__status) {
            case FsStatus::Success:
                return "success";
            case FsStatus::InvalidArgument:
                return "invalid-argument";
            case FsStatus::BufferTooSmall:
                return "buffer-too-small";
            case FsStatus::Overflow:
                return "overflow";
            case FsStatus::OutOfMemory:
                return "out-of-memory";
            case FsStatus::BlockError:
                return "block-error";
            case FsStatus::NoSupportedPartition:
                return "no-supported-partition";
            case FsStatus::MalformedFilesystem:
                return "malformed-filesystem";
            case FsStatus::Unsupported:
                return "unsupported";
            case FsStatus::NotFound:
                return "not-found";
            case FsStatus::NotRegularFile:
                return "not-regular-file";
            default:
                return "unknown";
        }
    }

    FsStatus find_exfat_partition(driver::block::BlockDevice *__device, Partition *__out) noexcept {
        if (__device == nullptr || __out == nullptr)
            return FsStatus::InvalidArgument;

        uint8_t sector[SECTOR_SIZE];
        FsStatus status = read_sector(__device, 0, sector);
        if (status != FsStatus::Success)
            return status;
        if (sector[MBR_SIGNATURE_OFFSET] != 0x55 || sector[MBR_SIGNATURE_OFFSET + 1] != 0xaa)
            return FsStatus::MalformedFilesystem;

        for (uint32_t i = 0; i < 4; i++) {
            const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
            if (entry[4] != EXFAT_PARTITION_TYPE)
                continue;
            Partition candidate = {le32(entry + 8), le32(entry + 12)};
            uint64_t end = 0;
            if (candidate.lba == 0 || candidate.sector_count == 0 || add_overflows(candidate.lba, candidate.sector_count, &end))
                continue;
            if (__device->total_sectors != 0 && end > __device->total_sectors)
                continue;
            if (validate_boot_region(__device, &candidate, nullptr) == FsStatus::Success) {
                *__out = candidate;
                return FsStatus::Success;
            }
        }
        return FsStatus::NoSupportedPartition;
    }

    FsStatus mount_exfat(driver::block::BlockDevice *__device, const Partition *__partition, ExfatMount *__out) noexcept {
        if (__device == nullptr || __partition == nullptr || __out == nullptr)
            return FsStatus::InvalidArgument;
        return validate_boot_region(__device, __partition, __out);
    }

    FsStatus lookup(ExfatMount *__mount, const char *__absolute_path, FileMetadata *__out) noexcept {
        if (__mount == nullptr || __absolute_path == nullptr || __out == nullptr || __absolute_path[0] != '/')
            return FsStatus::InvalidArgument;

        FileMetadata current = {__mount->root_directory_cluster, 0, 0, __mount->bytes_per_cluster, true, true};
        const char *cursor = __absolute_path + 1;
        if (*cursor == 0) {
            *__out = current;
            return FsStatus::Success;
        }

        while (*cursor != 0) {
            while (*cursor == '/')
                cursor++;
            const char *start = cursor;
            uint32_t len = 0;
            while (cursor[len] != 0 && cursor[len] != '/') {
                len++;
                if (len > MAX_COMPONENT_LENGTH)
                    return FsStatus::InvalidArgument;
            }
            if (len == 0)
                break;

            FileMetadata child = {};
            FsStatus status = find_child(__mount, &current, start, len, &child);
            if (status != FsStatus::Success)
                return status;
            cursor += len;
            while (*cursor == '/')
                cursor++;
            if (*cursor != 0 && !child.is_directory)
                return FsStatus::NotFound;
            current = child;
        }

        *__out = current;
        return FsStatus::Success;
    }

    ReadResult read_file(
        ExfatMount *__mount, const FileMetadata *__file, uint64_t __offset, void *__dst, size_t __len,
        size_t __dst_len) noexcept {
        if (__mount == nullptr || __file == nullptr || (__dst == nullptr && __len != 0))
            return {FsStatus::InvalidArgument, 0};
        if (__file->is_directory)
            return {FsStatus::NotRegularFile, 0};
        if (__dst_len < __len)
            return {FsStatus::BufferTooSmall, 0};
        if (__len == 0 || __offset >= __file->data_length)
            return {FsStatus::Success, 0};

        uint64_t available = __file->data_length - __offset;
        size_t to_read = __len;
        if ((uint64_t)to_read > available)
            to_read = (size_t)available;

        uint8_t *cluster_buffer = (uint8_t *)bigos::kmalloc(__mount->bytes_per_cluster);
        if (cluster_buffer == nullptr)
            return {FsStatus::OutOfMemory, 0};
        const size_t visited_bytes = (__mount->cluster_count + 7) / 8;
        uint8_t *visited = nullptr;
        if (!__file->no_fat_chain) {
            visited = (uint8_t *)bigos::kmalloc(visited_bytes);
            if (visited == nullptr) {
                bigos::free(cluster_buffer);
                return {FsStatus::OutOfMemory, 0};
            }
            memset(visited, 0, visited_bytes);
        }

        const uint64_t first_cluster_index = __offset / __mount->bytes_per_cluster;
        const uint64_t max_clusters =
            (__file->data_length + __mount->bytes_per_cluster - 1) / __mount->bytes_per_cluster;
        if (max_clusters > __mount->cluster_count) {
            bigos::free(cluster_buffer);
            return {FsStatus::MalformedFilesystem, 0};
        }

        uint32_t cluster = __file->first_cluster;
        FsStatus status = FsStatus::Success;
        for (uint64_t step = 0; step < first_cluster_index; step++) {
            if (mark_chain_visit(visited, cluster)) {
                bigos::free(visited);
                bigos::free(cluster_buffer);
                return {FsStatus::MalformedFilesystem, 0};
            }
            uint32_t next = 0;
            status = next_cluster(__mount, __file, cluster, &next);
            if (status != FsStatus::Success || next == 0) {
                if (visited != nullptr)
                    bigos::free(visited);
                bigos::free(cluster_buffer);
                return {status == FsStatus::Success ? FsStatus::MalformedFilesystem : status, 0};
            }
            cluster = next;
        }

        size_t copied = 0;
        uint64_t cluster_file_offset = first_cluster_index * (uint64_t)__mount->bytes_per_cluster;
        while (copied < to_read) {
            if (mark_chain_visit(visited, cluster)) {
                status = FsStatus::MalformedFilesystem;
                break;
            }
            status = read_cluster(__mount, cluster, cluster_buffer);
            if (status != FsStatus::Success)
                break;
            const uint64_t in_cluster_offset = (__offset + copied) - cluster_file_offset;
            size_t chunk = __mount->bytes_per_cluster - (size_t)in_cluster_offset;
            if (chunk > to_read - copied)
                chunk = to_read - copied;
            memcpy((uint8_t *)__dst + copied, cluster_buffer + in_cluster_offset, chunk);
            copied += chunk;
            cluster_file_offset += __mount->bytes_per_cluster;
            if (copied == to_read)
                break;
            uint32_t next = 0;
            status = next_cluster(__mount, __file, cluster, &next);
            if (status != FsStatus::Success)
                break;
            if (next == 0) {
                status = FsStatus::MalformedFilesystem;
                break;
            }
            cluster = next;
        }

        if (visited != nullptr)
            bigos::free(visited);
        bigos::free(cluster_buffer);
        return {status, copied};
    }

    FsStatus readdir(ExfatMount *__mount, const FileMetadata *__directory, uint64_t __offset, DirectoryEntry *__entries,
        size_t __max_entries, size_t *__entries_read, uint64_t *__next_offset) noexcept {
        if (__entries_read != nullptr)
            *__entries_read = 0;
        if (__next_offset != nullptr)
            *__next_offset = __offset;
        if (__mount == nullptr || __directory == nullptr || __entries == nullptr || __entries_read == nullptr ||
            __next_offset == nullptr || __max_entries == 0)
            return FsStatus::InvalidArgument;
        if (!__directory->is_directory || __directory->first_cluster < FIRST_DATA_CLUSTER)
            return FsStatus::NotRegularFile;

        uint8_t *cluster_buffer = (uint8_t *)bigos::kmalloc(__mount->bytes_per_cluster);
        if (cluster_buffer == nullptr)
            return FsStatus::OutOfMemory;
        const size_t visited_bytes = (__mount->cluster_count + 7) / 8;
        uint8_t *visited = nullptr;
        if (!__directory->no_fat_chain) {
            visited = (uint8_t *)bigos::kmalloc(visited_bytes);
            if (visited == nullptr) {
                bigos::free(cluster_buffer);
                return FsStatus::OutOfMemory;
            }
            memset(visited, 0, visited_bytes);
        }

        FsStatus status = FsStatus::Success;
        size_t emitted = 0;
        uint32_t cluster = __directory->first_cluster;
        const uint64_t total_len =
            __directory->data_length == 0 ? __mount->bytes_per_cluster : __directory->data_length;
        const uint64_t max_clusters = (total_len + __mount->bytes_per_cluster - 1) / __mount->bytes_per_cluster;
        for (uint64_t step = 0; step < max_clusters && emitted < __max_entries; step++) {
            if (mark_chain_visit(visited, cluster)) {
                status = FsStatus::MalformedFilesystem;
                break;
            }
            status = read_cluster(__mount, cluster, cluster_buffer);
            if (status != FsStatus::Success)
                break;

            size_t scan_len = __mount->bytes_per_cluster;
            const uint64_t cluster_base = step * (uint64_t)__mount->bytes_per_cluster;
            if (cluster_base + scan_len > total_len)
                scan_len = (size_t)(total_len - cluster_base);

            for (size_t offset = 0; offset + ENTRY_SIZE <= scan_len && emitted < __max_entries;) {
                const uint64_t absolute_offset = cluster_base + offset;
                const uint8_t entry_type = cluster_buffer[offset];
                if (entry_type == 0) {
                    *__next_offset = total_len;
                    status = FsStatus::Success;
                    goto done;
                }
                if (absolute_offset < __offset || entry_type != ENTRY_FILE) {
                    offset += ENTRY_SIZE;
                    continue;
                }

                size_t set_size = 0;
                status = parse_entry_set_for_listing(cluster_buffer, scan_len, offset, &__entries[emitted], &set_size);
                if (status != FsStatus::Success)
                    goto done;
                emitted++;
                offset += set_size;
                *__next_offset = cluster_base + offset;
            }

            if (step + 1 == max_clusters)
                break;
            uint32_t next = 0;
            status = next_cluster(__mount, __directory, cluster, &next);
            if (status != FsStatus::Success)
                break;
            if (next == 0) {
                status = FsStatus::MalformedFilesystem;
                break;
            }
            cluster = next;
        }

    done:
        if (visited != nullptr)
            bigos::free(visited);
        bigos::free(cluster_buffer);
        if (status == FsStatus::Success)
            *__entries_read = emitted;
        return status;
    }
}   // namespace fs
NAMESPACE_BIGOS_END
