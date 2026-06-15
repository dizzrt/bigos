#include <bigos/fs/vfs.h>

#include <bigos/cred.h>
#include <bigos/fs/bigfs.h>
#include <bigos/fs/exfat.h>
#include <bigos/memory.h>
#include <drivers/block/ata_pio.h>
#include <string.h>

namespace {
    struct ExfatFileState {
        bigos::fs::FileMetadata metadata;
    };

    // State for a bigfs-backed file object. The requester identity captured at
    // open time is reused as the writer identity for subsequent writes.
    struct BigfsFileState {
        uint32_t inode;
        uint32_t uid;
        uint32_t gid;
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
                return bigos::vfs::Status::InvalidArgument;
            case bigos::fs::FsStatus::BufferTooSmall:
                return bigos::vfs::Status::Range;
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

    constexpr size_t VFS_DIRENT_BATCH_MAX = 16;

    bigos::vfs::Status exfat_readdir(bigos::vfs::File *__file, bigos::vfs::DirectoryEntry *__entries,
        size_t __max_entries, size_t *__entries_read) noexcept {
        if (__entries_read != nullptr)
            *__entries_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        if (__entries == nullptr || __max_entries == 0)
            return bigos::vfs::Status::InvalidArgument;
        if (__file->vnode == nullptr || !__file->vnode->is_directory)
            return bigos::vfs::Status::NotDirectory;
        if (__max_entries > VFS_DIRENT_BATCH_MAX)
            return bigos::vfs::Status::Range;

        ExfatFileState *state = (ExfatFileState *)__file->private_data;
        bigos::fs::DirectoryEntry local_entries[VFS_DIRENT_BATCH_MAX];
        size_t count = 0;
        uint64_t next_offset = __file->offset;
        const bigos::fs::FsStatus status =
            bigos::fs::readdir(&g_mount, &state->metadata, __file->offset, local_entries, __max_entries, &count, &next_offset);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);
        for (size_t i = 0; i < count; i++) {
            __entries[i].type = local_entries[i].type == bigos::fs::EXFAT_DIRENT_TYPE_DIRECTORY ?
                                    bigos::vfs::DIRENT_TYPE_DIRECTORY :
                                    bigos::vfs::DIRENT_TYPE_FILE;
            memcpy(__entries[i].name, local_entries[i].name, sizeof(__entries[i].name));
        }
        __file->offset = next_offset;
        if (__entries_read != nullptr)
            *__entries_read = count;
        return bigos::vfs::Status::Success;
    }

    const bigos::vfs::FileOperations EXFAT_FILE_OPS = {&exfat_read, &exfat_close, nullptr, nullptr, &exfat_readdir};

    void fill_metadata_defaults(bigos::Metadata *__out) noexcept {
        if (__out == nullptr)
            return;
        *__out = {};
        __out->nlink = 1;
        __out->object_id = 0;
    }

    void fill_exfat_metadata(const bigos::fs::FileMetadata *__metadata, bigos::Metadata *__out) noexcept {
        fill_metadata_defaults(__out);
        if (__metadata == nullptr || __out == nullptr)
            return;
        __out->type =
            __metadata->is_directory ? bigos::BIGOS_METADATA_TYPE_DIRECTORY : bigos::BIGOS_METADATA_TYPE_REGULAR;
        __out->mode = __metadata->is_directory ? (bigos::BIGOS_MODE_IFDIR | 0555) : (bigos::BIGOS_MODE_IFREG | 0444);
        __out->uid = bigos::cred::ROOT_UID;
        __out->gid = 0;
        __out->size = __metadata->data_length;
    }

    bigos::vfs::Status fill_bigfs_metadata(uint32_t __inode, bigos::Metadata *__out) noexcept {
        fill_metadata_defaults(__out);
        if (__out == nullptr)
            return bigos::vfs::Status::InvalidArgument;
        uint32_t mode = 0;
        uint32_t uid = 0;
        uint32_t gid = 0;
        uint64_t size = 0;
        bool is_dir = false;
        if (!bigos::bigfs::stat(__inode, &mode, &uid, &gid, &size, &is_dir))
            return bigos::vfs::Status::NotFound;
        __out->type = is_dir ? bigos::BIGOS_METADATA_TYPE_DIRECTORY : bigos::BIGOS_METADATA_TYPE_REGULAR;
        __out->mode = (mode & 07777) | (is_dir ? bigos::BIGOS_MODE_IFDIR : bigos::BIGOS_MODE_IFREG);
        __out->uid = uid;
        __out->gid = gid;
        __out->size = size;
        return bigos::vfs::Status::Success;
    }

    bigos::vfs::Status bigfs_to_vfs(bigos::bigfs::Status __status) noexcept {
        switch (__status) {
            case bigos::bigfs::Status::Success:
                return bigos::vfs::Status::Success;
            case bigos::bigfs::Status::Invalid:
                return bigos::vfs::Status::InvalidArgument;
            case bigos::bigfs::Status::NotFound:
                return bigos::vfs::Status::NotFound;
            case bigos::bigfs::Status::Exists:
                return bigos::vfs::Status::Exists;
            case bigos::bigfs::Status::NoSpace:
                return bigos::vfs::Status::NoSpace;
            case bigos::bigfs::Status::NotDirectory:
                return bigos::vfs::Status::NotDirectory;
            case bigos::bigfs::Status::IsDirectory:
                return bigos::vfs::Status::IsDirectory;
            case bigos::bigfs::Status::NotEmpty:
                return bigos::vfs::Status::NotEmpty;
            case bigos::bigfs::Status::AccessDenied:
                return bigos::vfs::Status::AccessDenied;
            case bigos::bigfs::Status::IoError:
                return bigos::vfs::Status::BlockError;
        }
        return bigos::vfs::Status::InvalidArgument;
    }

    bigos::vfs::Status bigfs_read(
        bigos::vfs::File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        if (!__file->readable && __len != 0)
            return bigos::vfs::Status::BadFileDescriptor;
        if (__dst == nullptr && __len != 0)
            return bigos::vfs::Status::InvalidArgument;
        BigfsFileState *state = (BigfsFileState *)__file->private_data;
        size_t read = 0;
        const bigos::bigfs::Status status =
            bigos::bigfs::read(state->inode, __file->offset, __dst, __len, &read);
        if (status != bigos::bigfs::Status::Success)
            return bigfs_to_vfs(status);
        __file->offset += read;
        if (__bytes_read != nullptr)
            *__bytes_read = read;
        return bigos::vfs::Status::Success;
    }

    bigos::vfs::Status bigfs_write(
        bigos::vfs::File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        if (!__file->writable)
            return bigos::vfs::Status::BadFileDescriptor;
        if (__src == nullptr && __len != 0)
            return bigos::vfs::Status::InvalidArgument;
        BigfsFileState *state = (BigfsFileState *)__file->private_data;
        size_t written = 0;
        const bigos::bigfs::Status status =
            bigos::bigfs::write(state->inode, __file->offset, __src, __len, state->uid, state->gid, &written);
        if (status != bigos::bigfs::Status::Success)
            return bigfs_to_vfs(status);   // offset not advanced on failure
        __file->offset += written;
        if (__file->vnode != nullptr && __file->offset > __file->vnode->size)
            __file->vnode->size = __file->offset;
        if (__bytes_written != nullptr)
            *__bytes_written = written;
        return bigos::vfs::Status::Success;
    }

    void bigfs_close(bigos::vfs::File *__file) noexcept {
        if (__file == nullptr)
            return;
        if (__file->private_data != nullptr) {
            BigfsFileState *state = (BigfsFileState *)__file->private_data;
            bigos::bigfs::close_inode(state->inode);
            bigos::free(__file->private_data);
            __file->private_data = nullptr;
        }
        if (__file->vnode != nullptr) {
            bigos::free(__file->vnode);
            __file->vnode = nullptr;
        }
    }

    bigos::vfs::Status bigfs_readdir(bigos::vfs::File *__file, bigos::vfs::DirectoryEntry *__entries,
        size_t __max_entries, size_t *__entries_read) noexcept {
        if (__entries_read != nullptr)
            *__entries_read = 0;
        if (__file == nullptr || __file->private_data == nullptr)
            return bigos::vfs::Status::BadFileDescriptor;
        if (__entries == nullptr || __max_entries == 0)
            return bigos::vfs::Status::InvalidArgument;
        if (__file->vnode == nullptr || !__file->vnode->is_directory)
            return bigos::vfs::Status::NotDirectory;
        BigfsFileState *state = (BigfsFileState *)__file->private_data;
        if (__max_entries > VFS_DIRENT_BATCH_MAX)
            return bigos::vfs::Status::Range;
        bigos::bigfs::DirectoryEntry local_entries[VFS_DIRENT_BATCH_MAX];
        size_t count = 0;
        uint64_t next_offset = __file->offset;
        const bigos::bigfs::Status status =
            bigos::bigfs::readdir(state->inode, __file->offset, local_entries, __max_entries, &count, &next_offset);
        if (status != bigos::bigfs::Status::Success)
            return bigfs_to_vfs(status);
        for (size_t i = 0; i < count; i++) {
            __entries[i].type = local_entries[i].type;
            memcpy(__entries[i].name, local_entries[i].name, sizeof(__entries[i].name));
        }
        __file->offset = next_offset;
        if (__entries_read != nullptr)
            *__entries_read = count;
        return bigos::vfs::Status::Success;
    }

    const bigos::vfs::FileOperations BIGFS_FILE_OPS = {&bigfs_read, &bigfs_close, &bigfs_write, nullptr, &bigfs_readdir};
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
            case Status::ReadOnlyFs:
                return "read-only-fs";
            case Status::NoSpace:
                return "no-space";
            case Status::AccessDenied:
                return "access-denied";
            case Status::NotDirectory:
                return "not-directory";
            case Status::NotSeekable:
                return "not-seekable";
            case Status::Exists:
                return "exists";
            case Status::BrokenPipe:
                return "broken-pipe";
            case Status::Range:
                return "range";
            case Status::NotEmpty:
                return "not-empty";
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
        // Best-effort publish of the writable bigfs mount on a RAM-backed device.
        // Its failure must not affect the read-only exFAT path (decision 9).
        (void)bigos::bigfs::init();
        return Status::Success;
    }

    bool initialized() noexcept {
        return g_initialized;
    }

    Status resolve_path(const char *__path, const char *__cwd, char *__out, size_t __out_len) noexcept {
        if (__path == nullptr || __out == nullptr || __out_len < 2)
            return Status::InvalidArgument;
        if (__path[0] == 0)
            return Status::InvalidArgument;

        size_t in_len = 0;
        while (in_len <= MAX_PATH_LEN && __path[in_len] != 0)
            in_len++;
        if (in_len == 0 || in_len > MAX_PATH_LEN)
            return Status::InvalidArgument;

        __out[0] = '/';
        __out[1] = 0;
        size_t out_len = 1;
        const char *cursor = __path;

        if (__path[0] == '/') {
            while (*cursor == '/')
                cursor++;
        } else {
            if (__cwd == nullptr || __cwd[0] != '/')
                return Status::InvalidArgument;
            size_t cwd_len = 0;
            while (cwd_len <= MAX_PATH_LEN && __cwd[cwd_len] != 0)
                cwd_len++;
            if (cwd_len == 0 || cwd_len > MAX_PATH_LEN || cwd_len + 1 > __out_len)
                return Status::InvalidArgument;
            memcpy(__out, __cwd, cwd_len + 1);
            out_len = cwd_len;
        }

        while (*cursor != 0) {
            while (*cursor == '/')
                cursor++;
            if (*cursor == 0)
                break;

            const char *component = cursor;
            size_t component_len = 0;
            while (cursor[component_len] != 0 && cursor[component_len] != '/')
                component_len++;
            if (component_len > MAX_PATH_LEN)
                return Status::InvalidArgument;

            if (component_len == 1 && component[0] == '.') {
                cursor += component_len;
                continue;
            }
            if (component_len == 2 && component[0] == '.' && component[1] == '.') {
                if (out_len > 1) {
                    while (out_len > 1 && __out[out_len - 1] == '/')
                        out_len--;
                    while (out_len > 1 && __out[out_len - 1] != '/')
                        out_len--;
                    if (out_len > 1 && __out[out_len - 1] == '/')
                        out_len--;
                    __out[out_len] = 0;
                    if (out_len == 0) {
                        __out[0] = '/';
                        __out[1] = 0;
                        out_len = 1;
                    }
                }
                cursor += component_len;
                continue;
            }

            const size_t extra = (out_len == 1 ? 0 : 1) + component_len;
            if (component_len == 0 || out_len + extra > MAX_PATH_LEN || out_len + extra + 1 > __out_len)
                return Status::InvalidArgument;
            if (out_len != 1)
                __out[out_len++] = '/';
            memcpy(__out + out_len, component, component_len);
            out_len += component_len;
            __out[out_len] = 0;
            cursor += component_len;
        }

        return Status::Success;
    }

    Status open_absolute(const char *__path, uint64_t __flags, File **__out_file) noexcept {
        // Read-only convenience overload. Writable callers pass identity/mode.
        return open_absolute(__path, __flags & ~(OPEN_WRONLY | OPEN_RDWR | OPEN_CREAT | OPEN_TRUNC), 0,
            bigos::cred::ROOT_UID, 0, __out_file);
    }

    Status open_absolute(const char *__path, uint64_t __flags, uint32_t __mode, uint32_t __uid, uint32_t __gid,
        File **__out_file) noexcept {
        if (__out_file == nullptr)
            return Status::InvalidArgument;
        *__out_file = nullptr;
        if (!g_initialized)
            return Status::NotInitialized;
        if (__path == nullptr || __path[0] != '/')
            return Status::InvalidArgument;

        const bool wants_write = (__flags & (OPEN_WRONLY | OPEN_RDWR | OPEN_CREAT | OPEN_TRUNC)) != 0;

        // Writable bigfs mount.
        if (bigos::bigfs::owns_path(__path)) {
            uint32_t inode = 0;
            uint64_t size = 0;
            bool is_dir = false;
            const bigos::bigfs::Status status =
                bigos::bigfs::open(__path, __flags, __mode, __uid, __gid, &inode, &size, &is_dir);
            if (status != bigos::bigfs::Status::Success)
                return bigfs_to_vfs(status);

            Vnode *vnode = (Vnode *)bigos::kmalloc(sizeof(Vnode));
            if (vnode == nullptr) {
                bigos::bigfs::close_inode(inode);
                return Status::NoMemory;
            }
            BigfsFileState *state = (BigfsFileState *)bigos::kmalloc(sizeof(BigfsFileState));
            if (state == nullptr) {
                bigos::bigfs::close_inode(inode);
                bigos::free(vnode);
                return Status::NoMemory;
            }
            File *file = (File *)bigos::kmalloc(sizeof(File));
            if (file == nullptr) {
                bigos::bigfs::close_inode(inode);
                bigos::free(state);
                bigos::free(vnode);
                return Status::NoMemory;
            }
            state->inode = inode;
            state->uid = __uid;
            state->gid = __gid;
            vnode->size = size;
            vnode->is_directory = is_dir;
            vnode->private_data = state;
            file->ops = &BIGFS_FILE_OPS;
            file->vnode = vnode;
            file->offset = 0;
            file->ref_count = 1;
            file->readable = (__flags & OPEN_WRONLY) == 0;
            file->close_on_exec = false;
            file->private_data = state;
            file->writable = wants_write;
            *__out_file = file;
            return Status::Success;
        }

        // Read-only exFAT backend rejects any write/create request with EROFS.
        if (wants_write)
            return Status::ReadOnlyFs;
        if (!path_supported(__path))
            return Status::InvalidArgument;

        bigos::fs::FileMetadata metadata = {};
        bigos::fs::FsStatus status = bigos::fs::lookup(&g_mount, __path, &metadata);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);

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
        file->writable = false;
        *__out_file = file;
        return Status::Success;
    }

    Status open(const char *__path, const char *__cwd, uint64_t __flags, uint32_t __mode, uint32_t __uid,
        uint32_t __gid, File **__out_file) noexcept {
        char resolved[MAX_PATH_LEN + 1];
        const Status status = resolve_path(__path, __cwd, resolved, sizeof(resolved));
        if (status != Status::Success)
            return status;
        return open_absolute(resolved, __flags, __mode, __uid, __gid, __out_file);
    }

    Status read(File *__file, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        if (__file == nullptr || __file->ops == nullptr || __file->ops->read == nullptr)
            return Status::BadFileDescriptor;
        return __file->ops->read(__file, __dst, __len, __bytes_read);
    }

    Status write(File *__file, const void *__src, size_t __len, size_t *__bytes_written) noexcept {
        if (__bytes_written != nullptr)
            *__bytes_written = 0;
        if (__file == nullptr || __file->ops == nullptr)
            return Status::BadFileDescriptor;
        // A backend without a write op is read-only.
        if (__file->ops->write == nullptr)
            return Status::ReadOnlyFs;
        if (!__file->writable)
            return Status::BadFileDescriptor;
        return __file->ops->write(__file, __src, __len, __bytes_written);
    }

    Status lseek(File *__file, int64_t __offset, int __whence, uint64_t *__new_offset) noexcept {
        if (__file == nullptr || __file->ops == nullptr)
            return Status::BadFileDescriptor;
        // Backend-specific seek (pipes return NotSeekable).
        if (__file->ops->lseek != nullptr)
            return __file->ops->lseek(__file, __offset, __whence, __new_offset);

        uint64_t base = 0;
        switch (__whence) {
            case SEEK_SET:
                base = 0;
                break;
            case SEEK_CUR:
                base = __file->offset;
                break;
            case SEEK_END:
                base = __file->vnode != nullptr ? __file->vnode->size : 0;
                break;
            default:
                return Status::InvalidArgument;
        }
        // Compute base + offset with overflow / negative-result checks.
        if (__offset < 0) {
            const uint64_t neg = (uint64_t)(-(__offset + 1)) + 1ull;
            if (neg > base)
                return Status::InvalidArgument;
            __file->offset = base - neg;
        } else {
            if (base > UINT64_MAX - (uint64_t)__offset)
                return Status::InvalidArgument;
            __file->offset = base + (uint64_t)__offset;
        }
        if (__new_offset != nullptr)
            *__new_offset = __file->offset;
        return Status::Success;
    }

    Status fsync(File *__file) noexcept {
        if (__file == nullptr || __file->ops == nullptr)
            return Status::BadFileDescriptor;
        // Only the writable backend needs a flush; read-only files are no-ops.
        if (__file->ops->write == nullptr)
            return Status::Success;
        return bigfs_to_vfs(bigos::bigfs::fsync());
    }

    Status readdir(File *__file, DirectoryEntry *__entries, size_t __max_entries, size_t *__entries_read) noexcept {
        if (__entries_read != nullptr)
            *__entries_read = 0;
        if (__file == nullptr || __file->ops == nullptr)
            return Status::BadFileDescriptor;
        if (__file->ops->readdir == nullptr)
            return Status::NotDirectory;
        return __file->ops->readdir(__file, __entries, __max_entries, __entries_read);
    }

    Status stat_absolute(const char *__path, bigos::Metadata *__out) noexcept {
        fill_metadata_defaults(__out);
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (!g_initialized)
            return Status::NotInitialized;
        if (__path == nullptr || __path[0] != '/')
            return Status::InvalidArgument;

        if (bigos::bigfs::owns_path(__path)) {
            uint32_t inode = 0;
            uint64_t size = 0;
            bool is_dir = false;
            const bigos::bigfs::Status open_status =
                bigos::bigfs::open(__path, OPEN_RDONLY, 0, bigos::cred::ROOT_UID, 0, &inode, &size, &is_dir);
            if (open_status != bigos::bigfs::Status::Success)
                return bigfs_to_vfs(open_status);
            const Status status = fill_bigfs_metadata(inode, __out);
            bigos::bigfs::close_inode(inode);
            return status;
        }

        if (!path_supported(__path))
            return Status::InvalidArgument;
        bigos::fs::FileMetadata metadata = {};
        const bigos::fs::FsStatus status = bigos::fs::lookup(&g_mount, __path, &metadata);
        if (status != bigos::fs::FsStatus::Success)
            return fs_to_vfs(status);
        fill_exfat_metadata(&metadata, __out);
        return Status::Success;
    }

    Status stat_path(const char *__path, const char *__cwd, bigos::Metadata *__out) noexcept {
        fill_metadata_defaults(__out);
        char resolved[MAX_PATH_LEN + 1];
        const Status status = resolve_path(__path, __cwd, resolved, sizeof(resolved));
        if (status != Status::Success)
            return status;
        return stat_absolute(resolved, __out);
    }

    Status stat(File *__file, bigos::Metadata *__out) noexcept {
        fill_metadata_defaults(__out);
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (__file == nullptr || __file->ops == nullptr)
            return Status::BadFileDescriptor;
        if (__file->vnode == nullptr)
            return Status::Unsupported;

        if (__file->ops == &EXFAT_FILE_OPS) {
            ExfatFileState *state = (ExfatFileState *)__file->private_data;
            if (state == nullptr)
                return Status::BadFileDescriptor;
            fill_exfat_metadata(&state->metadata, __out);
            return Status::Success;
        }
        if (__file->ops == &BIGFS_FILE_OPS) {
            BigfsFileState *state = (BigfsFileState *)__file->private_data;
            if (state == nullptr)
                return Status::BadFileDescriptor;
            return fill_bigfs_metadata(state->inode, __out);
        }

        __out->type = __file->vnode->is_directory ? BIGOS_METADATA_TYPE_DIRECTORY : BIGOS_METADATA_TYPE_REGULAR;
        __out->mode = __file->vnode->is_directory ? (BIGOS_MODE_IFDIR | 0555) : (BIGOS_MODE_IFREG | 0444);
        __out->uid = bigos::cred::ROOT_UID;
        __out->gid = 0;
        __out->size = __file->vnode->size;
        return Status::Success;
    }

    Status mkdir(const char *__path, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized)
            return Status::NotInitialized;
        if (__path == nullptr || __path[0] != '/')
            return Status::InvalidArgument;
        if (!bigos::bigfs::owns_path(__path))
            return Status::ReadOnlyFs;
        return bigfs_to_vfs(bigos::bigfs::mkdir(__path, __mode, __uid, __gid));
    }

    Status mkdir(const char *__path, const char *__cwd, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept {
        char resolved[MAX_PATH_LEN + 1];
        const Status status = resolve_path(__path, __cwd, resolved, sizeof(resolved));
        if (status != Status::Success)
            return status;
        return mkdir(resolved, __mode, __uid, __gid);
    }

    Status unlink(const char *__path, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized)
            return Status::NotInitialized;
        if (__path == nullptr || __path[0] != '/')
            return Status::InvalidArgument;
        if (!bigos::bigfs::owns_path(__path))
            return Status::ReadOnlyFs;
        return bigfs_to_vfs(bigos::bigfs::unlink(__path, __uid, __gid));
    }

    Status unlink(const char *__path, const char *__cwd, uint32_t __uid, uint32_t __gid) noexcept {
        char resolved[MAX_PATH_LEN + 1];
        const Status status = resolve_path(__path, __cwd, resolved, sizeof(resolved));
        if (status != Status::Success)
            return status;
        return unlink(resolved, __uid, __gid);
    }

    Status rename(const char *__old_path, const char *__new_path, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized)
            return Status::NotInitialized;
        if (__old_path == nullptr || __new_path == nullptr || __old_path[0] != '/' || __new_path[0] != '/')
            return Status::InvalidArgument;
        if (!bigos::bigfs::owns_path(__old_path) || !bigos::bigfs::owns_path(__new_path))
            return Status::ReadOnlyFs;
        return bigfs_to_vfs(bigos::bigfs::rename(__old_path, __new_path, __uid, __gid));
    }

    Status rename(const char *__old_path, const char *__new_path, const char *__cwd, uint32_t __uid,
        uint32_t __gid) noexcept {
        char old_resolved[MAX_PATH_LEN + 1];
        char new_resolved[MAX_PATH_LEN + 1];
        Status status = resolve_path(__old_path, __cwd, old_resolved, sizeof(old_resolved));
        if (status != Status::Success)
            return status;
        status = resolve_path(__new_path, __cwd, new_resolved, sizeof(new_resolved));
        if (status != Status::Success)
            return status;
        return rename(old_resolved, new_resolved, __uid, __gid);
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
