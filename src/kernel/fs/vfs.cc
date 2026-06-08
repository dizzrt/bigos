#include <bigos/fs/vfs.h>

#include <bigos/fs/exfat.h>
#include <bigos/memory.h>
#include <drivers/block/ata_pio.h>
#include <string.h>

namespace {
    struct ExfatFileState {
        bigos::fs::FileMetadata metadata;
    };

    driver::block::AtaPioDevice g_ata = {};
    bigos::fs::ExfatMount g_mount = {};
    bigos::vfs::Vnode g_root = {};
    bool g_initialized = false;

    bool add_overflow(uint64_t __a, uint64_t __b, uint64_t *__out) noexcept {
        if (__a > UINT64_MAX - __b)
            return true;
        if (__out != nullptr)
            *__out = __a + __b;
        return false;
    }

    bigos::vfs::Status fs_to_vfs(bigos::fs::FsStatus __status) noexcept {
        switch (__status) {
            case bigos::fs::FsStatus::Success:
                return bigos::vfs::Status::Success;
            case bigos::fs::FsStatus::InvalidArgument:
            case bigos::fs::FsStatus::BufferTooSmall:
                return bigos::vfs::Status::InvalidArgument;
            case bigos::fs::FsStatus::Overflow:
                return bigos::vfs::Status::Overflow;
            case bigos::fs::FsStatus::OutOfMemory:
                return bigos::vfs::Status::NoMemory;
            case bigos::fs::FsStatus::BlockError:
                return bigos::vfs::Status::BlockError;
            case bigos::fs::FsStatus::NoSupportedPartition:
            case bigos::fs::FsStatus::MalformedFilesystem:
            case bigos::fs::FsStatus::Unsupported:
                return bigos::vfs::Status::Unsupported;
            case bigos::fs::FsStatus::NotFound:
                return bigos::vfs::Status::NotFound;
            case bigos::fs::FsStatus::NotRegularFile:
                return bigos::vfs::Status::NotRegularFile;
        }
        return bigos::vfs::Status::Unsupported;
    }

    bool readonly_flags(uint64_t __flags) noexcept {
        const uint64_t write_flags =
            bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_RDWR | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC;
        return (__flags & write_flags) == 0;
    }

    bool path_supported(const char *__path) noexcept {
        if (__path == nullptr || __path[0] != '/')
            return false;

        size_t len = 0;
        while (len <= bigos::vfs::MAX_PATH_LEN) {
            const char ch = __path[len];
            if (ch == 0)
                break;
            len++;
        }
        if (len == 0 || len > bigos::vfs::MAX_PATH_LEN)
            return false;

        const char *cursor = __path + 1;
        while (*cursor != 0) {
            while (*cursor == '/')
                cursor++;
            const char *start = cursor;
            size_t component_len = 0;
            while (cursor[component_len] != 0 && cursor[component_len] != '/')
                component_len++;
            if ((component_len == 1 && start[0] == '.') || (component_len == 2 && start[0] == '.' && start[1] == '.'))
                return false;
            cursor += component_len;
        }
        return true;
    }

    bigos::vfs::Status exfat_read(bigos::vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || (!__file->readable && __len != 0))
            return bigos::vfs::Status::BadFileDescriptor;
        if (__dst == nullptr && __len != 0)
            return bigos::vfs::Status::InvalidArgument;

        ExfatFileState *state = (ExfatFileState *)__file->private_data;
        if (state == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        if (__file->offset >= state->metadata.data_length) {
            if (__bytes_read != nullptr)
                *__bytes_read = 0;
            return bigos::vfs::Status::Success;
        }
        uint64_t end = 0;
        if (add_overflow(__file->offset, (uint64_t)__len, &end))
            return bigos::vfs::Status::Overflow;

        bigos::fs::ReadResult result =
            bigos::fs::read_file(&g_mount, &state->metadata, __file->offset, __dst, __len, __len);
        if (result.status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(result.status);

        __file->offset += result.bytes_read;
        if (__bytes_read != nullptr)
            *__bytes_read = result.bytes_read;
        return bigos::vfs::Status::Success;
    }

    void exfat_close(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr)
            return;
        if (__file->private_data != nullptr) {
            bigos::free(__file->private_data);
            __file->private_data = nullptr;
        }
        if (__file->vnode != nullptr) {
            bigos::free(__file->vnode);
            __file->vnode = nullptr;
        }
    }

    const bigos::vfs::FileOperations EXFAT_FILE_OPS = {&exfat_read, &exfat_close};
}   // namespace

NAMESPACE_BIGOS_BEG
namespace vfs {
    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Success:
                return "success";
            case Status::InvalidArgument:
                return "invalid-argument";
            case Status::BadFileDescriptor:
                return "bad-file-descriptor";
            case Status::NotInitialized:
                return "not-initialized";
            case Status::NoMemory:
                return "no-memory";
            case Status::NotFound:
                return "not-found";
            case Status::NotRegularFile:
                return "not-regular-file";
            case Status::Unsupported:
                return "unsupported";
            case Status::Overflow:
                return "overflow";
            case Status::BlockError:
                return "block-error";
            case Status::WouldBlock:
                return "would-block";
        }
        return "unknown";
    }

    Status init() noexcept {
        if (g_initialized)
            return Status::Success;

        driver::block::AtaPioDevice ata = {};
        driver::block::ata_pio_primary_master_init(&ata);

        bigos::fs::Partition partition = {};
        bigos::fs::FsStatus status = bigos::fs::find_exfat_partition(&ata.block, &partition);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);

        bigos::fs::ExfatMount mount = {};
        status = bigos::fs::mount_exfat(&ata.block, &partition, &mount);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);

        g_ata = ata;
        g_ata.block.context = &g_ata;
        mount.device = &g_ata.block;
        g_mount = mount;
        g_root.size = g_mount.bytes_per_cluster;
        g_root.is_directory = true;
        g_root.private_data = nullptr;
        g_initialized = true;
        return Status::Success;
    }

    bool initialized() noexcept {
        return g_initialized;
    }

    Status open_absolute(const char *__path, uint64_t __flags, File **__out_file) noexcept {
        if (__out_file == nullptr)
            return Status::InvalidArgument;
        *__out_file = nullptr;
        if (!g_initialized)
            return Status::NotInitialized;
        if (!readonly_flags(__flags) || !path_supported(__path))
            return Status::InvalidArgument;

        bigos::fs::FileMetadata metadata = {};
        bigos::fs::FsStatus status = bigos::fs::lookup(&g_mount, __path, &metadata);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);
        if (metadata.is_directory)
            return Status::NotRegularFile;

        Vnode *vnode = (Vnode *)bigos::kmalloc(sizeof(Vnode));
        if (vnode == nullptr)
            return Status::NoMemory;
        ExfatFileState *state = (ExfatFileState *)bigos::kmalloc(sizeof(ExfatFileState));
        if (state == nullptr) {
            bigos::free(vnode);
            return Status::NoMemory;
        }
        File *file = (File *)bigos::kmalloc(sizeof(File));
        if (file == nullptr) {
            bigos::free(state);
            bigos::free(vnode);
            return Status::NoMemory;
        }

        state->metadata = metadata;
        vnode->size = metadata.data_length;
        vnode->is_directory = metadata.is_directory;
        vnode->private_data = state;
        file->ops = &EXFAT_FILE_OPS;
        file->vnode = vnode;
        file->offset = 0;
        file->ref_count = 1;
        file->readable = true;
        file->close_on_exec = false;
        file->private_data = state;
        *__out_file = file;
        return Status::Success;
    }

    Status read(File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->ops == nullptr || __file->ops->read == nullptr)
            return Status::BadFileDescriptor;
        return __file->ops->read(__file, __dst, __len, __bytes_read);
    }

    void retain(File *__file) noexcept {
        if (__file != nullptr)
            __file->ref_count++;
    }

    void release(File *__file) noexcept {
        if (__file == nullptr)
            return;
        if (__file->ref_count > 1) {
            __file->ref_count--;
            return;
        }
        if (__file->ops != nullptr && __file->ops->close != nullptr)
            __file->ops->close(__file);
        bigos::free(__file);
    }
}   // namespace vfs
NAMESPACE_BIGOS_END
