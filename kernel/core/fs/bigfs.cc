#include <bigos/fs/bigfs.h>

#include <bigos/cred.h>
#include <bigos/fs/bcache.h>
#include <bigos/fs/vfs.h>
#include <bigos/memory.h>
#include <string.h>

// Internal allocator flag: alloc_kernel_pages() only returns mapped, accessible
// backing when pre-paging is requested. The RAM-disk region is touched
// immediately, so request pre-paged kernel pages.
#include "../../mm/memdef.h"

namespace {
    using bigos::bigfs::BLOCK_SIZE;
    using bigos::bigfs::DATA_START;
    using bigos::bigfs::DIRECT_BLOCKS;
    using bigos::bigfs::INODE_COUNT;
    using bigos::bigfs::INODE_SIZE;
    using bigos::bigfs::INODE_TABLE_START;
    using bigos::bigfs::INODES_PER_BLOCK;
    using bigos::bigfs::Status;

    constexpr uint32_t INODE_BITMAP_BLOCK = 1;
    constexpr uint32_t DATA_BITMAP_BLOCK = 2;
    constexpr uint32_t DIRENTS_PER_BLOCK = BLOCK_SIZE / bigos::bigfs::DIRENT_SIZE;   // 16

    // 60 bytes packed into the 64-byte on-disk inode slot.
    struct DiskInode {
        uint32_t type;
        uint32_t mode;
        uint32_t uid;
        uint32_t gid;
        uint64_t size;
        uint32_t link_count;
        uint32_t direct[DIRECT_BLOCKS];
    };

    struct DiskDirent {
        uint32_t inode_plus_one;   // 0 means empty slot
        char name[bigos::bigfs::DIRENT_NAME_MAX + 1];
    };

    uint8_t *g_ram = nullptr;
    driver::block::BlockDevice g_device = {};
    bool g_initialized = false;

    driver::block::BlockStatus ram_read_impl(
        driver::block::BlockDevice *, uint64_t __lba, uint32_t __count, void *__dst, size_t __dst_len) noexcept {
        if (__lba + __count > bigos::bigfs::TOTAL_BLOCKS)
            return driver::block::BlockStatus::Overflow;
        const size_t bytes = (size_t)__count * BLOCK_SIZE;
        if (__dst_len < bytes)
            return driver::block::BlockStatus::BufferTooSmall;
        memcpy(__dst, g_ram + __lba * BLOCK_SIZE, bytes);
        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus ram_write_impl(
        driver::block::BlockDevice *, uint64_t __lba, uint32_t __count, const void *__src, size_t __src_len) noexcept {
        if (__lba + __count > bigos::bigfs::TOTAL_BLOCKS)
            return driver::block::BlockStatus::Overflow;
        const size_t bytes = (size_t)__count * BLOCK_SIZE;
        if (__src_len < bytes)
            return driver::block::BlockStatus::BufferTooSmall;
        memcpy(g_ram + __lba * BLOCK_SIZE, __src, bytes);
        return driver::block::BlockStatus::Success;
    }

    bool str_eq(const char *__a, const char *__b) noexcept {
        size_t i = 0;
        while (__a[i] != 0 && __b[i] != 0) {
            if (__a[i] != __b[i])
                return false;
            i++;
        }
        return __a[i] == __b[i];
    }

    // --- bitmap helpers (operate on a single bitmap block via the cache) ---

    Status bitmap_alloc(uint32_t __bitmap_block, uint32_t __count, uint32_t *__out_index) noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, __bitmap_block);
        if (block == nullptr)
            return Status::IoError;
        Status status = Status::NoSpace;
        for (uint32_t i = 0; i < __count; i++) {
            const uint32_t byte = i / 8;
            const uint8_t mask = (uint8_t)(1u << (i % 8));
            if ((block->data[byte] & mask) == 0) {
                block->data[byte] |= mask;
                bigos::bcache::mark_dirty(block);
                *__out_index = i;
                status = Status::Success;
                break;
            }
        }
        bigos::bcache::put(block);
        return status;
    }

    void bitmap_free(uint32_t __bitmap_block, uint32_t __index) noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, __bitmap_block);
        if (block == nullptr)
            return;
        const uint32_t byte = __index / 8;
        const uint8_t mask = (uint8_t)(1u << (__index % 8));
        block->data[byte] &= (uint8_t)~mask;
        bigos::bcache::mark_dirty(block);
        bigos::bcache::put(block);
    }

    void bitmap_set(uint32_t __bitmap_block, uint32_t __index) noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, __bitmap_block);
        if (block == nullptr)
            return;
        const uint32_t byte = __index / 8;
        const uint8_t mask = (uint8_t)(1u << (__index % 8));
        block->data[byte] |= mask;
        bigos::bcache::mark_dirty(block);
        bigos::bcache::put(block);
    }

    // --- inode helpers ---

    bool load_inode(uint32_t __inode, DiskInode *__out) noexcept {
        if (__inode >= INODE_COUNT)
            return false;
        const uint32_t block_no = INODE_TABLE_START + __inode / INODES_PER_BLOCK;
        const uint32_t off = (__inode % INODES_PER_BLOCK) * INODE_SIZE;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
        if (block == nullptr)
            return false;
        memcpy(__out, block->data + off, sizeof(DiskInode));
        bigos::bcache::put(block);
        return true;
    }

    bool store_inode(uint32_t __inode, const DiskInode *__in) noexcept {
        if (__inode >= INODE_COUNT)
            return false;
        const uint32_t block_no = INODE_TABLE_START + __inode / INODES_PER_BLOCK;
        const uint32_t off = (__inode % INODES_PER_BLOCK) * INODE_SIZE;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
        if (block == nullptr)
            return false;
        memcpy(block->data + off, __in, sizeof(DiskInode));
        bigos::bcache::mark_dirty(block);
        bigos::bcache::put(block);
        return true;
    }

    Status alloc_inode(uint32_t *__out_inode) noexcept {
        return bitmap_alloc(INODE_BITMAP_BLOCK, INODE_COUNT, __out_inode);
    }

    // Allocates a data block, zeroes it, and returns its absolute block number.
    Status alloc_data_block(uint32_t *__out_block) noexcept {
        uint32_t index = 0;
        const Status status = bitmap_alloc(DATA_BITMAP_BLOCK, bigos::bigfs::DATA_BLOCK_COUNT, &index);
        if (status != Status::Success)
            return status;
        const uint32_t block_no = DATA_START + index;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
        if (block == nullptr) {
            bitmap_free(DATA_BITMAP_BLOCK, index);
            return Status::IoError;
        }
        memset(block->data, 0, BLOCK_SIZE);
        bigos::bcache::mark_dirty(block);
        bigos::bcache::put(block);
        *__out_block = block_no;
        return Status::Success;
    }

    void free_data_block(uint32_t __block_no) noexcept {
        if (__block_no < DATA_START)
            return;
        bitmap_free(DATA_BITMAP_BLOCK, __block_no - DATA_START);
    }

    void release_inode_blocks(DiskInode *__inode) noexcept {
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            if (__inode->direct[i] != 0) {
                free_data_block(__inode->direct[i]);
                __inode->direct[i] = 0;
            }
        }
        __inode->size = 0;
    }

    // --- directory helpers ---

    bool dir_lookup(const DiskInode *__dir, const char *__name, uint32_t *__out_inode) noexcept {
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            const uint32_t block_no = __dir->direct[i];
            if (block_no == 0)
                continue;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
            if (block == nullptr)
                continue;
            for (uint32_t e = 0; e < DIRENTS_PER_BLOCK; e++) {
                DiskDirent *ent = (DiskDirent *)(block->data + e * bigos::bigfs::DIRENT_SIZE);
                if (ent->inode_plus_one != 0 && str_eq(ent->name, __name)) {
                    *__out_inode = ent->inode_plus_one - 1;
                    bigos::bcache::put(block);
                    return true;
                }
            }
            bigos::bcache::put(block);
        }
        return false;
    }

    Status dir_add_entry(uint32_t __dir_inode, DiskInode *__dir, const char *__name, uint32_t __child) noexcept {
        // Reuse a free slot in an existing block.
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            const uint32_t block_no = __dir->direct[i];
            if (block_no == 0)
                continue;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
            if (block == nullptr)
                return Status::IoError;
            for (uint32_t e = 0; e < DIRENTS_PER_BLOCK; e++) {
                DiskDirent *ent = (DiskDirent *)(block->data + e * bigos::bigfs::DIRENT_SIZE);
                if (ent->inode_plus_one == 0) {
                    ent->inode_plus_one = __child + 1;
                    memset(ent->name, 0, sizeof(ent->name));
                    size_t n = 0;
                    while (__name[n] != 0 && n < bigos::bigfs::DIRENT_NAME_MAX) {
                        ent->name[n] = __name[n];
                        n++;
                    }
                    bigos::bcache::mark_dirty(block);
                    bigos::bcache::put(block);
                    return Status::Success;
                }
            }
            bigos::bcache::put(block);
        }
        // Grow the directory by one data block.
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            if (__dir->direct[i] != 0)
                continue;
            uint32_t new_block = 0;
            const Status status = alloc_data_block(&new_block);
            if (status != Status::Success)
                return status;
            __dir->direct[i] = new_block;
            __dir->size += BLOCK_SIZE;
            if (!store_inode(__dir_inode, __dir)) {
                free_data_block(new_block);
                __dir->direct[i] = 0;
                return Status::IoError;
            }
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, new_block);
            if (block == nullptr)
                return Status::IoError;
            DiskDirent *ent = (DiskDirent *)block->data;
            ent->inode_plus_one = __child + 1;
            memset(ent->name, 0, sizeof(ent->name));
            size_t n = 0;
            while (__name[n] != 0 && n < bigos::bigfs::DIRENT_NAME_MAX) {
                ent->name[n] = __name[n];
                n++;
            }
            bigos::bcache::mark_dirty(block);
            bigos::bcache::put(block);
            return Status::Success;
        }
        return Status::NoSpace;
    }

    bool dir_remove_entry(const DiskInode *__dir, const char *__name) noexcept {
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            const uint32_t block_no = __dir->direct[i];
            if (block_no == 0)
                continue;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
            if (block == nullptr)
                continue;
            for (uint32_t e = 0; e < DIRENTS_PER_BLOCK; e++) {
                DiskDirent *ent = (DiskDirent *)(block->data + e * bigos::bigfs::DIRENT_SIZE);
                if (ent->inode_plus_one != 0 && str_eq(ent->name, __name)) {
                    ent->inode_plus_one = 0;
                    memset(ent->name, 0, sizeof(ent->name));
                    bigos::bcache::mark_dirty(block);
                    bigos::bcache::put(block);
                    return true;
                }
            }
            bigos::bcache::put(block);
        }
        return false;
    }

    bool dir_is_empty(const DiskInode *__dir) noexcept {
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            const uint32_t block_no = __dir->direct[i];
            if (block_no == 0)
                continue;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
            if (block == nullptr)
                continue;
            for (uint32_t e = 0; e < DIRENTS_PER_BLOCK; e++) {
                DiskDirent *ent = (DiskDirent *)(block->data + e * bigos::bigfs::DIRENT_SIZE);
                if (ent->inode_plus_one != 0) {
                    bigos::bcache::put(block);
                    return false;
                }
            }
            bigos::bcache::put(block);
        }
        return true;
    }

    // --- path resolution ---

    // Strips the mount prefix and yields the relative remainder. Returns false
    // when the path does not target this mount. *__rest points after the prefix
    // and is "" for the mount root.
    bool strip_prefix(const char *__abs_path, const char **__rest) noexcept {
        const char *p = bigos::bigfs::MOUNT_PREFIX;
        size_t i = 0;
        while (p[i] != 0) {
            if (__abs_path[i] != p[i])
                return false;
            i++;
        }
        if (__abs_path[i] != 0 && __abs_path[i] != '/')
            return false;
        *__rest = __abs_path + i;
        return true;
    }

    // Resolves the parent directory inode of the final component and copies the
    // final component name into __leaf (bounded). Returns Success with
    // __parent_inode and __leaf set, or an error. Empty remainder (mount root)
    // returns Invalid because root is not creatable/removable here.
    Status resolve_parent(const char *__rest, uint32_t *__parent_inode, char *__leaf, size_t __leaf_cap) noexcept {
        uint32_t cur = bigos::bigfs::ROOT_INODE;
        const char *cursor = __rest;
        // Skip leading slashes.
        while (*cursor == '/')
            cursor++;
        if (*cursor == 0)
            return Status::Invalid;

        for (;;) {
            const char *start = cursor;
            size_t len = 0;
            while (cursor[len] != 0 && cursor[len] != '/')
                len++;
            if (len == 0 || len >= __leaf_cap)
                return Status::Invalid;

            // Determine if this is the last component.
            const char *after = start + len;
            while (*after == '/')
                after++;
            const bool last = (*after == 0);

            if (last) {
                size_t k = 0;
                for (; k < len; k++)
                    __leaf[k] = start[k];
                __leaf[k] = 0;
                *__parent_inode = cur;
                return Status::Success;
            }

            // Intermediate: must be an existing directory.
            char comp[bigos::bigfs::DIRENT_NAME_MAX + 1];
            if (len > bigos::bigfs::DIRENT_NAME_MAX)
                return Status::Invalid;
            for (size_t k = 0; k < len; k++)
                comp[k] = start[k];
            comp[len] = 0;

            DiskInode dir;
            if (!load_inode(cur, &dir) || dir.type != bigos::bigfs::INODE_DIRECTORY)
                return Status::NotDirectory;
            uint32_t child = 0;
            if (!dir_lookup(&dir, comp, &child))
                return Status::NotFound;
            cur = child;
            cursor = after;
        }
    }

    Status check_access(uint32_t __uid, uint32_t __gid, const DiskInode *__inode, bigos::cred::Access __access) noexcept {
        if (bigos::cred::permits(__inode->uid, __inode->gid, __inode->mode, __uid, __gid, __access))
            return Status::Success;
        return Status::AccessDenied;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace bigfs {
    bool init() noexcept {
        if (g_initialized)
            return true;
        const uint64_t total_bytes = (uint64_t)TOTAL_BLOCKS * BLOCK_SIZE;
        const uint32_t pages = (uint32_t)((total_bytes + 0xfff) / 0x1000);
        uint8_t *ram = (uint8_t *)bigos::alloc_kernel_pages(pages, _GFM_PRE_PAGING);
        if (ram == nullptr)
            return false;
        memset(ram, 0, (size_t)total_bytes);
        g_ram = ram;

        g_device.sector_size = BLOCK_SIZE;
        g_device.total_sectors = TOTAL_BLOCKS;
        g_device.context = nullptr;
        g_device.read_impl = ram_read_impl;
        g_device.write_impl = ram_write_impl;

        if (!bigos::bcache::init()) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }

        // Format: superblock magic, root inode bitmap bit, root directory inode.
        bigos::bcache::BufferBlock *sb = bigos::bcache::get(&g_device, 0);
        if (sb == nullptr) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }
        memset(sb->data, 0, BLOCK_SIZE);
        *(uint32_t *)sb->data = MAGIC;
        bigos::bcache::mark_dirty(sb);
        bigos::bcache::put(sb);

        bitmap_set(INODE_BITMAP_BLOCK, ROOT_INODE);

        DiskInode root = {};
        root.type = INODE_DIRECTORY;
        root.mode = 0755;
        root.uid = bigos::cred::ROOT_UID;
        root.gid = 0;
        root.size = 0;
        root.link_count = 2;
        if (!store_inode(ROOT_INODE, &root)) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }
        if (bigos::bcache::sync_all() != bigos::bcache::Status::Success) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }

        g_initialized = true;
        return true;
    }

    bool initialized() noexcept {
        return g_initialized;
    }

    driver::block::BlockDevice *device() noexcept {
        return g_initialized ? &g_device : nullptr;
    }

    bool owns_path(const char *__abs_path) noexcept {
        if (__abs_path == nullptr)
            return false;
        const char *rest = nullptr;
        return strip_prefix(__abs_path, &rest);
    }

    Status open(const char *__abs_path, uint64_t __flags, uint32_t __mode, uint32_t __uid, uint32_t __gid,
        uint32_t *__out_inode, uint64_t *__out_size) noexcept {
        if (!g_initialized || __abs_path == nullptr || __out_inode == nullptr || __out_size == nullptr)
            return Status::Invalid;
        const char *rest = nullptr;
        if (!strip_prefix(__abs_path, &rest))
            return Status::Invalid;

        uint32_t parent = 0;
        char leaf[DIRENT_NAME_MAX + 1];
        const Status rp = resolve_parent(rest, &parent, leaf, sizeof(leaf));
        if (rp != Status::Success)
            return rp;

        DiskInode dir;
        if (!load_inode(parent, &dir) || dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;

        uint32_t inode_num = 0;
        const bool exists = dir_lookup(&dir, leaf, &inode_num);
        const bool want_write =
            (__flags & (bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_RDWR | bigos::vfs::OPEN_TRUNC)) != 0;

        if (!exists) {
            if ((__flags & bigos::vfs::OPEN_CREAT) == 0)
                return Status::NotFound;
            // Creating requires write permission on the parent directory.
            const Status acc = check_access(__uid, __gid, &dir, bigos::cred::Access::Write);
            if (acc != Status::Success)
                return acc;
            uint32_t new_inode = 0;
            const Status ai = alloc_inode(&new_inode);
            if (ai != Status::Success)
                return ai;
            DiskInode file = {};
            file.type = INODE_REGULAR;
            file.mode = __mode;
            file.uid = __uid;
            file.gid = __gid;
            file.size = 0;
            file.link_count = 1;
            if (!store_inode(new_inode, &file)) {
                bitmap_free(INODE_BITMAP_BLOCK, new_inode);
                return Status::IoError;
            }
            const Status add = dir_add_entry(parent, &dir, leaf, new_inode);
            if (add != Status::Success) {
                bitmap_free(INODE_BITMAP_BLOCK, new_inode);
                return add;
            }
            *__out_inode = new_inode;
            *__out_size = 0;
            return Status::Success;
        }

        // Existing file.
        DiskInode file;
        if (!load_inode(inode_num, &file))
            return Status::IoError;
        if (file.type == INODE_DIRECTORY)
            return Status::IsDirectory;
        if (want_write) {
            const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Write);
            if (acc != Status::Success)
                return acc;
        } else {
            const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Read);
            if (acc != Status::Success)
                return acc;
        }
        if ((__flags & bigos::vfs::OPEN_TRUNC) != 0) {
            release_inode_blocks(&file);
            if (!store_inode(inode_num, &file))
                return Status::IoError;
        }
        *__out_inode = inode_num;
        *__out_size = file.size;
        return Status::Success;
    }

    Status read(uint32_t __inode, uint64_t __offset, void *__dst, size_t __len, size_t *__out_read) noexcept {
        if (__out_read != nullptr)
            *__out_read = 0;
        if (!g_initialized || __dst == nullptr)
            return Status::Invalid;
        DiskInode file;
        if (!load_inode(__inode, &file))
            return Status::Invalid;
        if (file.type != INODE_REGULAR)
            return Status::IsDirectory;
        if (__offset >= file.size)
            return Status::Success;

        uint64_t remaining = file.size - __offset;
        if ((uint64_t)__len < remaining)
            remaining = __len;
        size_t done = 0;
        uint8_t *out = (uint8_t *)__dst;
        while (remaining > 0) {
            const uint32_t block_index = (uint32_t)((__offset + done) / BLOCK_SIZE);
            const uint32_t block_off = (uint32_t)((__offset + done) % BLOCK_SIZE);
            if (block_index >= DIRECT_BLOCKS)
                break;
            uint32_t chunk = BLOCK_SIZE - block_off;
            if ((uint64_t)chunk > remaining)
                chunk = (uint32_t)remaining;
            const uint32_t block_no = file.direct[block_index];
            if (block_no == 0) {
                memset(out + done, 0, chunk);   // sparse hole
            } else {
                bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
                if (block == nullptr)
                    return Status::IoError;
                memcpy(out + done, block->data + block_off, chunk);
                bigos::bcache::put(block);
            }
            done += chunk;
            remaining -= chunk;
        }
        if (__out_read != nullptr)
            *__out_read = done;
        return Status::Success;
    }

    Status write(uint32_t __inode, uint64_t __offset, const void *__src, size_t __len, uint32_t __uid, uint32_t __gid,
        size_t *__out_written) noexcept {
        if (__out_written != nullptr)
            *__out_written = 0;
        if (!g_initialized || __src == nullptr)
            return Status::Invalid;
        if (__len == 0)
            return Status::Success;
        if (__offset >= MAX_FILE_SIZE)
            return Status::NoSpace;

        DiskInode file;
        if (!load_inode(__inode, &file))
            return Status::Invalid;
        if (file.type != INODE_REGULAR)
            return Status::IsDirectory;
        const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Write);
        if (acc != Status::Success)
            return acc;

        uint64_t end = __offset + __len;
        if (end > MAX_FILE_SIZE)
            end = MAX_FILE_SIZE;
        const uint64_t writable = end - __offset;

        size_t done = 0;
        const uint8_t *in = (const uint8_t *)__src;
        while ((uint64_t)done < writable) {
            const uint32_t block_index = (uint32_t)((__offset + done) / BLOCK_SIZE);
            const uint32_t block_off = (uint32_t)((__offset + done) % BLOCK_SIZE);
            if (block_index >= DIRECT_BLOCKS)
                break;
            uint32_t chunk = BLOCK_SIZE - block_off;
            if ((uint64_t)chunk > writable - done)
                chunk = (uint32_t)(writable - done);

            if (file.direct[block_index] == 0) {
                uint32_t new_block = 0;
                const Status ab = alloc_data_block(&new_block);
                if (ab != Status::Success) {
                    // Persist whatever we have written so far; deterministic NoSpace.
                    if (done > 0) {
                        if (__offset + done > file.size)
                            file.size = __offset + done;
                        (void)store_inode(__inode, &file);
                        if (__out_written != nullptr)
                            *__out_written = done;
                        return Status::Success;
                    }
                    return ab;
                }
                file.direct[block_index] = new_block;
            }
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, file.direct[block_index]);
            if (block == nullptr)
                return done > 0 ? Status::Success : Status::IoError;
            memcpy(block->data + block_off, in + done, chunk);
            bigos::bcache::mark_dirty(block);
            bigos::bcache::put(block);
            done += chunk;
        }

        if (__offset + done > file.size)
            file.size = __offset + done;
        if (!store_inode(__inode, &file))
            return done > 0 ? Status::Success : Status::IoError;
        if (__out_written != nullptr)
            *__out_written = done;
        return Status::Success;
    }

    Status mkdir(const char *__abs_path, uint32_t __mode, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized || __abs_path == nullptr)
            return Status::Invalid;
        const char *rest = nullptr;
        if (!strip_prefix(__abs_path, &rest))
            return Status::Invalid;
        uint32_t parent = 0;
        char leaf[DIRENT_NAME_MAX + 1];
        const Status rp = resolve_parent(rest, &parent, leaf, sizeof(leaf));
        if (rp != Status::Success)
            return rp;

        DiskInode dir;
        if (!load_inode(parent, &dir) || dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;
        uint32_t existing = 0;
        if (dir_lookup(&dir, leaf, &existing))
            return Status::Exists;
        const Status acc = check_access(__uid, __gid, &dir, bigos::cred::Access::Write);
        if (acc != Status::Success)
            return acc;

        uint32_t new_inode = 0;
        const Status ai = alloc_inode(&new_inode);
        if (ai != Status::Success)
            return ai;
        DiskInode child = {};
        child.type = INODE_DIRECTORY;
        child.mode = __mode;
        child.uid = __uid;
        child.gid = __gid;
        child.size = 0;
        child.link_count = 2;
        if (!store_inode(new_inode, &child)) {
            bitmap_free(INODE_BITMAP_BLOCK, new_inode);
            return Status::IoError;
        }
        const Status add = dir_add_entry(parent, &dir, leaf, new_inode);
        if (add != Status::Success) {
            bitmap_free(INODE_BITMAP_BLOCK, new_inode);
            return add;
        }
        return Status::Success;
    }

    Status unlink(const char *__abs_path, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized || __abs_path == nullptr)
            return Status::Invalid;
        const char *rest = nullptr;
        if (!strip_prefix(__abs_path, &rest))
            return Status::Invalid;
        uint32_t parent = 0;
        char leaf[DIRENT_NAME_MAX + 1];
        const Status rp = resolve_parent(rest, &parent, leaf, sizeof(leaf));
        if (rp != Status::Success)
            return rp;

        DiskInode dir;
        if (!load_inode(parent, &dir) || dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;
        uint32_t target = 0;
        if (!dir_lookup(&dir, leaf, &target))
            return Status::NotFound;
        const Status acc = check_access(__uid, __gid, &dir, bigos::cred::Access::Write);
        if (acc != Status::Success)
            return acc;

        DiskInode tnode;
        if (!load_inode(target, &tnode))
            return Status::IoError;
        if (tnode.type == INODE_DIRECTORY)
            return Status::IsDirectory;

        if (!dir_remove_entry(&dir, leaf))
            return Status::NotFound;
        release_inode_blocks(&tnode);
        tnode.type = INODE_FREE;
        tnode.link_count = 0;
        (void)store_inode(target, &tnode);
        bitmap_free(INODE_BITMAP_BLOCK, target);
        return Status::Success;
    }

    Status fsync() noexcept {
        if (!g_initialized)
            return Status::Invalid;
        if (bigos::bcache::sync_all() != bigos::bcache::Status::Success)
            return Status::IoError;
        return Status::Success;
    }

    bool stat(uint32_t __inode, uint32_t *__mode, uint32_t *__uid, uint32_t *__gid, uint64_t *__size,
        bool *__is_dir) noexcept {
        DiskInode node;
        if (!g_initialized || !load_inode(__inode, &node) || node.type == INODE_FREE)
            return false;
        if (__mode != nullptr)
            *__mode = node.mode;
        if (__uid != nullptr)
            *__uid = node.uid;
        if (__gid != nullptr)
            *__gid = node.gid;
        if (__size != nullptr)
            *__size = node.size;
        if (__is_dir != nullptr)
            *__is_dir = node.type == INODE_DIRECTORY;
        return true;
    }
}   // namespace bigfs
NAMESPACE_BIGOS_END
