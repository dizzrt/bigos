#include <bigos/fs/bigfs.h>

#include <bigos/cred.h>
#include <bigos/fs/bcache.h>
#include <bigos/fs/vfs.h>
#include <bigos/memory.h>
#include <drivers/block/ata_pio.h>
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
    using bigos::bigfs::ROOT_INODE;
    using bigos::bigfs::Status;

    constexpr uint32_t INODE_BITMAP_BLOCK = 1;
    constexpr uint32_t DATA_BITMAP_BLOCK = 2;
    constexpr uint32_t DIRENTS_PER_BLOCK = BLOCK_SIZE / bigos::bigfs::DIRENT_SIZE;   // 16
    constexpr uint32_t SUPERBLOCK_CHECKSUM_SEED = 0x9e3779b9u;

    struct DiskSuperblock {
        uint32_t magic;
        uint32_t version;
        uint32_t block_size;
        uint32_t total_blocks;
        uint32_t inode_count;
        uint32_t inode_size;
        uint32_t inode_table_start;
        uint32_t inode_table_blocks;
        uint32_t data_start;
        uint32_t data_block_count;
        uint32_t root_inode;
        uint32_t checksum;
    };

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

    struct DirSlot {
        bigos::bcache::BufferBlock *block;
        DiskDirent *entry;
    };

    bool load_inode(uint32_t __inode, DiskInode *__out) noexcept;

    uint8_t *g_ram = nullptr;
    driver::block::BlockDevice g_device = {};
    driver::block::AtaPioDevice g_persistent_ata = {};
    uint32_t g_open_refs[INODE_COUNT] = {};
    uint8_t g_write_staged[DIRECT_BLOCKS][BLOCK_SIZE] = {};
    bool g_initialized = false;
    bool g_persistent = false;
    uint8_t g_validate_inode_bitmap[BLOCK_SIZE] = {};
    uint8_t g_validate_data_bitmap[BLOCK_SIZE] = {};
    uint8_t g_validate_data_owner[bigos::bigfs::DATA_BLOCK_COUNT] = {};
    DiskInode g_validate_inodes[INODE_COUNT] = {};

    constexpr uint32_t METADATA_COMMIT_BLOCKS_MAX = 64;
    struct MetadataCommitPlan {
        uint32_t blocks[METADATA_COMMIT_BLOCKS_MAX];
        uint32_t count;
        bool pending;
    };
    MetadataCommitPlan g_metadata_commit = {};

    uint32_t inode_table_block_no(uint32_t __inode) noexcept {
        return INODE_TABLE_START + __inode / INODES_PER_BLOCK;
    }

    void metadata_commit_reset() noexcept {
        g_metadata_commit.count = 0;
        g_metadata_commit.pending = false;
    }

    bool metadata_commit_add(uint32_t __block_no) noexcept {
        if (__block_no >= bigos::bigfs::TOTAL_BLOCKS)
            return false;
        for (uint32_t i = 0; i < g_metadata_commit.count; i++) {
            if (g_metadata_commit.blocks[i] == __block_no)
                return true;
        }
        if (g_metadata_commit.count >= METADATA_COMMIT_BLOCKS_MAX)
            return false;
        g_metadata_commit.blocks[g_metadata_commit.count++] = __block_no;
        g_metadata_commit.pending = true;
        return true;
    }

    bool metadata_commit_inode(uint32_t __inode) noexcept {
        if (__inode >= INODE_COUNT)
            return false;
        return metadata_commit_add(inode_table_block_no(__inode));
    }

    bool metadata_commit_inode_data(const DiskInode *__inode) noexcept {
        if (__inode == nullptr)
            return false;
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            if (__inode->direct[i] != 0 && !metadata_commit_add(__inode->direct[i]))
                return false;
        }
        return true;
    }

    bool metadata_commit_flush() noexcept {
        for (uint32_t i = 0; i < g_metadata_commit.count; i++) {
            if (bigos::bcache::sync_block(&g_device, g_metadata_commit.blocks[i]) !=
                bigos::bcache::Status::Success)
                return false;
        }
        metadata_commit_reset();
        return true;
    }

    Status metadata_commit_file_create(uint32_t __parent_inode, const DiskInode *__parent, uint32_t __child_inode,
        const DiskInode *__child) noexcept {
        metadata_commit_reset();
        if (!metadata_commit_add(INODE_BITMAP_BLOCK) || !metadata_commit_inode_data(__child) ||
            !metadata_commit_inode(__child_inode) || !metadata_commit_inode_data(__parent) ||
            !metadata_commit_inode(__parent_inode))
            return Status::NoSpace;
        return metadata_commit_flush() ? Status::Success : Status::IoError;
    }

    Status metadata_commit_directory_create(
        uint32_t __parent_inode, const DiskInode *__parent, uint32_t __child_inode, const DiskInode *__child) noexcept {
        return metadata_commit_file_create(__parent_inode, __parent, __child_inode, __child);
    }

    Status metadata_commit_unlink(uint32_t __parent_inode, const DiskInode *__parent, uint32_t __target_inode,
        const DiskInode *__target, const DiskInode *__old_target, bool __released) noexcept {
        metadata_commit_reset();
        if (!metadata_commit_inode_data(__parent) || !metadata_commit_inode(__parent_inode) ||
            !metadata_commit_inode_data(__target) || !metadata_commit_inode(__target_inode))
            return Status::NoSpace;
        if (__old_target != nullptr) {
            for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
                if (__old_target->direct[i] != 0 && !metadata_commit_add(__old_target->direct[i]))
                    return Status::NoSpace;
            }
        }
        if (__released) {
            if (!metadata_commit_add(DATA_BITMAP_BLOCK) || !metadata_commit_add(INODE_BITMAP_BLOCK))
                return Status::NoSpace;
        }
        return metadata_commit_flush() ? Status::Success : Status::IoError;
    }

    Status metadata_commit_rename(
        uint32_t __old_parent, const DiskInode *__old_dir, uint32_t __new_parent, const DiskInode *__new_dir) noexcept {
        metadata_commit_reset();
        if (!metadata_commit_inode_data(__new_dir) || !metadata_commit_inode(__new_parent) ||
            !metadata_commit_inode_data(__old_dir) || !metadata_commit_inode(__old_parent))
            return Status::NoSpace;
        if (__old_parent != __new_parent && !metadata_commit_add(DATA_BITMAP_BLOCK))
            return Status::NoSpace;
        return metadata_commit_flush() ? Status::Success : Status::IoError;
    }

    Status metadata_commit_growth(uint32_t __inode, const DiskInode *__file, uint32_t *__data_blocks,
        uint32_t __data_count, bool __allocated_blocks) noexcept {
        metadata_commit_reset();
        for (uint32_t i = 0; i < __data_count; i++) {
            if (__data_blocks[i] != 0 && !metadata_commit_add(__data_blocks[i]))
                return Status::NoSpace;
        }
        if (!metadata_commit_inode_data(__file))
            return Status::NoSpace;
        if (__allocated_blocks && !metadata_commit_add(DATA_BITMAP_BLOCK))
            return Status::NoSpace;
        if (!metadata_commit_inode(__inode))
            return Status::NoSpace;
        return metadata_commit_flush() ? Status::Success : Status::IoError;
    }

    Status metadata_commit_truncate(uint32_t __inode, const DiskInode *__committed, const DiskInode *__old,
        uint32_t __tail_block, bool __released_blocks) noexcept {
        metadata_commit_reset();
        if (__tail_block != 0 && !metadata_commit_add(__tail_block))
            return Status::NoSpace;
        if (!metadata_commit_inode_data(__committed) || !metadata_commit_inode(__inode))
            return Status::NoSpace;
        if (__released_blocks) {
            if (__old != nullptr) {
                for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
                    if (__old->direct[i] != 0 && !metadata_commit_add(__old->direct[i]))
                        return Status::NoSpace;
                }
            }
            if (!metadata_commit_add(DATA_BITMAP_BLOCK))
                return Status::NoSpace;
        }
        return metadata_commit_flush() ? Status::Success : Status::IoError;
    }

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

    uint32_t superblock_checksum(const DiskSuperblock *__sb) noexcept {
        if (__sb == nullptr)
            return 0;
        const uint32_t *words = (const uint32_t *)__sb;
        uint32_t value = SUPERBLOCK_CHECKSUM_SEED;
        for (size_t i = 0; i < sizeof(DiskSuperblock) / sizeof(uint32_t) - 1; i++)
            value = (value << 5) ^ (value >> 2) ^ words[i];
        return value;
    }

    DiskSuperblock make_superblock() noexcept {
        DiskSuperblock sb = {};
        sb.magic = bigos::bigfs::MAGIC;
        sb.version = bigos::bigfs::FORMAT_VERSION;
        sb.block_size = BLOCK_SIZE;
        sb.total_blocks = bigos::bigfs::TOTAL_BLOCKS;
        sb.inode_count = INODE_COUNT;
        sb.inode_size = INODE_SIZE;
        sb.inode_table_start = INODE_TABLE_START;
        sb.inode_table_blocks = bigos::bigfs::INODE_TABLE_BLOCKS;
        sb.data_start = DATA_START;
        sb.data_block_count = bigos::bigfs::DATA_BLOCK_COUNT;
        sb.root_inode = ROOT_INODE;
        sb.checksum = superblock_checksum(&sb);
        return sb;
    }

    bool bitmap_bytes_test(const uint8_t *__bitmap, uint32_t __index) noexcept {
        const uint32_t byte = __index / 8;
        const uint8_t mask = (uint8_t)(1u << (__index % 8));
        return (__bitmap[byte] & mask) != 0;
    }

    Status read_bitmap_block(uint32_t __block_no, uint8_t *__out) noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, __block_no);
        if (block == nullptr)
            return Status::IoError;
        memcpy(__out, block->data, BLOCK_SIZE);
        bigos::bcache::put(block);
        return Status::Success;
    }

    bool validate_inode_shape(uint32_t __inode_index, const DiskInode *__inode, bool __bitmap_used) noexcept {
        if (__inode == nullptr)
            return false;
        if (!__bitmap_used)
            return __inode->type == bigos::bigfs::INODE_FREE;
        if (__inode->type != bigos::bigfs::INODE_REGULAR && __inode->type != bigos::bigfs::INODE_DIRECTORY)
            return false;
        if (__inode_index == ROOT_INODE && __inode->type != bigos::bigfs::INODE_DIRECTORY)
            return false;
        if (__inode->link_count == 0 || __inode->size > bigos::bigfs::MAX_FILE_SIZE)
            return false;
        if (__inode->type == bigos::bigfs::INODE_DIRECTORY && (__inode->size % BLOCK_SIZE) != 0)
            return false;
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            const uint32_t block_no = __inode->direct[i];
            if (block_no != 0 && (block_no < DATA_START || block_no >= bigos::bigfs::TOTAL_BLOCKS))
                return false;
        }
        return true;
    }

    Status validate_metadata_invariants() noexcept {
        memset(g_validate_inode_bitmap, 0, sizeof(g_validate_inode_bitmap));
        memset(g_validate_data_bitmap, 0, sizeof(g_validate_data_bitmap));
        memset(g_validate_data_owner, 0, sizeof(g_validate_data_owner));
        memset(g_validate_inodes, 0, sizeof(g_validate_inodes));
        Status status = read_bitmap_block(INODE_BITMAP_BLOCK, g_validate_inode_bitmap);
        if (status != Status::Success)
            return status;
        status = read_bitmap_block(DATA_BITMAP_BLOCK, g_validate_data_bitmap);
        if (status != Status::Success)
            return status;

        for (uint32_t i = 0; i < INODE_COUNT; i++) {
            if (!load_inode(i, &g_validate_inodes[i]))
                return Status::IoError;
            if (!validate_inode_shape(i, &g_validate_inodes[i], bitmap_bytes_test(g_validate_inode_bitmap, i)))
                return Status::Invalid;
            if (g_validate_inodes[i].type == bigos::bigfs::INODE_FREE)
                continue;
            for (uint32_t d = 0; d < DIRECT_BLOCKS; d++) {
                const uint32_t block_no = g_validate_inodes[i].direct[d];
                if (block_no == 0)
                    continue;
                const uint32_t data_index = block_no - DATA_START;
                if (!bitmap_bytes_test(g_validate_data_bitmap, data_index) || g_validate_data_owner[data_index] != 0)
                    return Status::Invalid;
                g_validate_data_owner[data_index] = 1;
            }
        }

        for (uint32_t i = 0; i < bigos::bigfs::DATA_BLOCK_COUNT; i++) {
            if (bitmap_bytes_test(g_validate_data_bitmap, i) && g_validate_data_owner[i] == 0)
                return Status::Invalid;
        }

        for (uint32_t i = 0; i < INODE_COUNT; i++) {
            if (g_validate_inodes[i].type != bigos::bigfs::INODE_DIRECTORY)
                continue;
            for (uint32_t d = 0; d < DIRECT_BLOCKS; d++) {
                const uint32_t block_no = g_validate_inodes[i].direct[d];
                if (block_no == 0)
                    continue;
                bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
                if (block == nullptr)
                    return Status::IoError;
                for (uint32_t e = 0; e < DIRENTS_PER_BLOCK; e++) {
                    DiskDirent *ent = (DiskDirent *)(block->data + e * bigos::bigfs::DIRENT_SIZE);
                    if (ent->inode_plus_one == 0)
                        continue;
                    const uint32_t child = ent->inode_plus_one - 1;
                    if (child >= INODE_COUNT || !bitmap_bytes_test(g_validate_inode_bitmap, child) ||
                        g_validate_inodes[child].type == bigos::bigfs::INODE_FREE) {
                        bigos::bcache::put(block);
                        return Status::Invalid;
                    }
                }
                bigos::bcache::put(block);
            }
        }
        return Status::Success;
    }

    Status validate_superblock() noexcept {
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, 0);
        if (block == nullptr)
            return Status::IoError;
        DiskSuperblock sb = {};
        memcpy(&sb, block->data, sizeof(sb));
        bigos::bcache::put(block);
        if (sb.magic != bigos::bigfs::MAGIC || sb.version != bigos::bigfs::FORMAT_VERSION ||
            sb.block_size != BLOCK_SIZE || sb.total_blocks != bigos::bigfs::TOTAL_BLOCKS ||
            sb.inode_count != INODE_COUNT || sb.inode_size != INODE_SIZE ||
            sb.inode_table_start != INODE_TABLE_START ||
            sb.inode_table_blocks != bigos::bigfs::INODE_TABLE_BLOCKS || sb.data_start != DATA_START ||
            sb.data_block_count != bigos::bigfs::DATA_BLOCK_COUNT || sb.root_inode != ROOT_INODE ||
            sb.checksum != superblock_checksum(&sb))
            return Status::Invalid;

        DiskInode root = {};
        if (!load_inode(ROOT_INODE, &root) || root.type != bigos::bigfs::INODE_DIRECTORY ||
            root.link_count == 0 || root.size > bigos::bigfs::MAX_FILE_SIZE)
            return Status::Invalid;
        return validate_metadata_invariants();
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
        const uint32_t block_no = inode_table_block_no(__inode);
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
        const uint32_t block_no = inode_table_block_no(__inode);
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

    Status bitmap_count_free(uint32_t __bitmap_block, uint32_t __count, uint32_t *__out_free) noexcept {
        if (__out_free == nullptr)
            return Status::Invalid;
        *__out_free = 0;
        bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, __bitmap_block);
        if (block == nullptr)
            return Status::IoError;
        uint32_t free_count = 0;
        for (uint32_t i = 0; i < __count; i++) {
            const uint32_t byte = i / 8;
            const uint8_t mask = (uint8_t)(1u << (i % 8));
            if ((block->data[byte] & mask) == 0)
                free_count++;
        }
        bigos::bcache::put(block);
        *__out_free = free_count;
        return Status::Success;
    }

    void discard_new_inode(uint32_t __inode) noexcept {
        if (__inode >= INODE_COUNT)
            return;
        DiskInode empty = {};
        (void)store_inode(__inode, &empty);
        bitmap_free(INODE_BITMAP_BLOCK, __inode);
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

    Status reserve_data_block(uint32_t *__out_block) noexcept {
        uint32_t index = 0;
        const Status status = bitmap_alloc(DATA_BITMAP_BLOCK, bigos::bigfs::DATA_BLOCK_COUNT, &index);
        if (status != Status::Success)
            return status;
        *__out_block = DATA_START + index;
        return Status::Success;
    }

    void free_data_block(uint32_t __block_no) noexcept {
        if (__block_no < DATA_START)
            return;
        bitmap_free(DATA_BITMAP_BLOCK, __block_no - DATA_START);
    }

    void rollback_allocated_blocks(uint32_t *__blocks, uint32_t __count) noexcept {
        if (__blocks == nullptr)
            return;
        for (uint32_t i = 0; i < __count; i++) {
            if (__blocks[i] != 0) {
                free_data_block(__blocks[i]);
                __blocks[i] = 0;
            }
        }
    }

    void put_pinned_blocks(bigos::bcache::BufferBlock **__blocks, uint32_t __count) noexcept {
        if (__blocks == nullptr)
            return;
        for (uint32_t i = 0; i < __count; i++) {
            if (__blocks[i] != nullptr) {
                bigos::bcache::put(__blocks[i]);
                __blocks[i] = nullptr;
            }
        }
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

    void maybe_free_unlinked_inode(uint32_t __inode, DiskInode *__node) noexcept {
        if (__inode == ROOT_INODE || __inode >= INODE_COUNT || __node == nullptr)
            return;
        if (__node->type == bigos::bigfs::INODE_FREE || __node->link_count != 0 || g_open_refs[__inode] != 0)
            return;
        release_inode_blocks(__node);
        __node->type = bigos::bigfs::INODE_FREE;
        (void)store_inode(__inode, __node);
        bitmap_free(INODE_BITMAP_BLOCK, __inode);
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

    bool dir_find_slot(const DiskInode *__dir, const char *__name, DirSlot *__out) noexcept {
        if (__out == nullptr)
            return false;
        __out->block = nullptr;
        __out->entry = nullptr;
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
                    __out->block = block;
                    __out->entry = ent;
                    return true;
                }
            }
            bigos::bcache::put(block);
        }
        return false;
    }

    Status dir_reserve_slot(uint32_t __dir_inode, DiskInode *__dir, DirSlot *__out) noexcept {
        if (__out == nullptr)
            return Status::Invalid;
        __out->block = nullptr;
        __out->entry = nullptr;
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
                    __out->block = block;
                    __out->entry = ent;
                    return Status::Success;
                }
            }
            bigos::bcache::put(block);
        }
        for (uint32_t i = 0; i < DIRECT_BLOCKS; i++) {
            if (__dir->direct[i] != 0)
                continue;
            uint32_t new_block = 0;
            const Status status = alloc_data_block(&new_block);
            if (status != Status::Success)
                return status;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, new_block);
            if (block == nullptr) {
                free_data_block(new_block);
                return Status::IoError;
            }
            DiskInode committed = *__dir;
            committed.direct[i] = new_block;
            committed.size += BLOCK_SIZE;
            if (!store_inode(__dir_inode, &committed)) {
                bigos::bcache::put(block);
                free_data_block(new_block);
                return Status::IoError;
            }
            *__dir = committed;
            __out->block = block;
            __out->entry = (DiskDirent *)block->data;
            return Status::Success;
        }
        return Status::NoSpace;
    }

    void dirent_set(DiskDirent *__entry, const char *__name, uint32_t __child) noexcept {
        __entry->inode_plus_one = __child + 1;
        memset(__entry->name, 0, sizeof(__entry->name));
        size_t n = 0;
        while (__name[n] != 0 && n < bigos::bigfs::DIRENT_NAME_MAX) {
            __entry->name[n] = __name[n];
            n++;
        }
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
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, new_block);
            if (block == nullptr) {
                free_data_block(new_block);
                return Status::IoError;
            }
            DiskDirent *ent = (DiskDirent *)block->data;
            ent->inode_plus_one = __child + 1;
            memset(ent->name, 0, sizeof(ent->name));
            size_t n = 0;
            while (__name[n] != 0 && n < bigos::bigfs::DIRENT_NAME_MAX) {
                ent->name[n] = __name[n];
                n++;
            }
            DiskInode committed = *__dir;
            committed.direct[i] = new_block;
            committed.size += BLOCK_SIZE;
            if (!store_inode(__dir_inode, &committed)) {
                bigos::bcache::put(block);
                free_data_block(new_block);
                return Status::IoError;
            }
            *__dir = committed;
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

    Status check_access(
        uint32_t __uid, uint32_t __gid, const DiskInode *__inode, bigos::cred::Access __access) noexcept {
        if (bigos::cred::permits(__inode->uid, __inode->gid, __inode->mode, __uid, __gid, __access))
            return Status::Success;
        return Status::AccessDenied;
    }

    Status format_current_device() noexcept {
        uint8_t zero[BLOCK_SIZE] = {};
        for (uint32_t block = 0; block < bigos::bigfs::TOTAL_BLOCKS; block++) {
            const driver::block::BlockStatus write_status =
                driver::block::write_sectors(&g_device, block, 1, zero, sizeof(zero));
            if (write_status != driver::block::BlockStatus::Success)
                return Status::IoError;
        }

        bigos::bcache::BufferBlock *sb_block = bigos::bcache::get(&g_device, 0);
        if (sb_block == nullptr)
            return Status::IoError;
        memset(sb_block->data, 0, BLOCK_SIZE);
        const DiskSuperblock sb = make_superblock();
        memcpy(sb_block->data, &sb, sizeof(sb));
        bigos::bcache::mark_dirty(sb_block);
        bigos::bcache::put(sb_block);

        bitmap_set(INODE_BITMAP_BLOCK, ROOT_INODE);

        DiskInode root = {};
        root.type = bigos::bigfs::INODE_DIRECTORY;
        root.mode = 0755;
        root.uid = bigos::cred::ROOT_UID;
        root.gid = 0;
        root.size = 0;
        root.link_count = 2;
        if (!store_inode(ROOT_INODE, &root))
            return Status::IoError;
        metadata_commit_reset();
        if (!metadata_commit_add(0) || !metadata_commit_add(INODE_BITMAP_BLOCK) || !metadata_commit_inode(ROOT_INODE))
            return Status::NoSpace;
        if (!metadata_commit_flush())
            return Status::IoError;
        if (bigos::bcache::invalidate_device(&g_device) != bigos::bcache::Status::Success)
            return Status::IoError;
        return validate_superblock();
    }

    bool any_open_refs() noexcept {
        for (uint32_t i = 0; i < INODE_COUNT; i++)
            if (g_open_refs[i] != 0)
                return true;
        return false;
    }

    bool publish_persistent_if_valid() noexcept {
#ifdef BIGOS_PERSISTENT_WRITABLE_FS
        driver::block::ata_pio_persistent_test_init(&g_persistent_ata);
        g_device = g_persistent_ata.block;
        g_device.context = &g_persistent_ata;
        g_device.total_sectors = bigos::bigfs::TOTAL_BLOCKS;
        if (!bigos::bcache::init())
            return false;
        if (validate_superblock() != Status::Success) {
            (void)bigos::bcache::invalidate_device(&g_device);
            return false;
        }
        memset(g_open_refs, 0, sizeof(g_open_refs));
        g_persistent = true;
        g_initialized = true;
        return true;
#else
        return false;
#endif
    }

    bool publish_ram_formatted() noexcept {
        const uint64_t total_bytes = (uint64_t)bigos::bigfs::TOTAL_BLOCKS * BLOCK_SIZE;
        const uint32_t pages = (uint32_t)((total_bytes + 0xfff) / 0x1000);
        uint8_t *ram = (uint8_t *)bigos::alloc_kernel_pages(pages, _GFM_PRE_PAGING);
        if (ram == nullptr)
            return false;
        memset(ram, 0, (size_t)total_bytes);
        g_ram = ram;

        g_device.sector_size = BLOCK_SIZE;
        g_device.total_sectors = bigos::bigfs::TOTAL_BLOCKS;
        g_device.context = nullptr;
        g_device.read_impl = ram_read_impl;
        g_device.write_impl = ram_write_impl;

        if (!bigos::bcache::init()) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }
        if (format_current_device() != Status::Success) {
            bigos::free(ram);
            g_ram = nullptr;
            return false;
        }
        memset(g_open_refs, 0, sizeof(g_open_refs));
        g_persistent = false;
        g_initialized = true;
        return true;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace bigfs {
    bool init() noexcept {
        if (g_initialized)
            return true;
        if (publish_persistent_if_valid())
            return true;
        return publish_ram_formatted();
    }

    Status format_persistent() noexcept {
#ifndef BIGOS_PERSISTENT_WRITABLE_FS
        return Status::Unsupported;
#else
        if (g_initialized && any_open_refs())
            return Status::AccessDenied;
        if (g_initialized && bigos::bcache::invalidate_device(&g_device) != bigos::bcache::Status::Success)
            return Status::IoError;
        driver::block::ata_pio_persistent_test_init(&g_persistent_ata);
        g_device = g_persistent_ata.block;
        g_device.context = &g_persistent_ata;
        g_device.total_sectors = TOTAL_BLOCKS;
        if (!bigos::bcache::init())
            return Status::IoError;
        const Status status = format_current_device();
        if (status != Status::Success) {
            g_initialized = false;
            g_persistent = false;
            return status;
        }
        memset(g_open_refs, 0, sizeof(g_open_refs));
        g_initialized = true;
        g_persistent = true;
        return Status::Success;
#endif
    }

    bool initialized() noexcept {
        return g_initialized;
    }

    bool persistent() noexcept {
        return g_initialized && g_persistent;
    }

    const char *backend_name() noexcept {
        if (!g_initialized)
            return "unpublished";
        return g_persistent ? "persistent clean-sync /rw" : "RAM-backed current-session /rw";
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
        uint32_t *__out_inode, uint64_t *__out_size, bool *__out_is_dir) noexcept {
        if (!g_initialized || __abs_path == nullptr || __out_inode == nullptr || __out_size == nullptr ||
            __out_is_dir == nullptr)
            return Status::Invalid;
        const char *rest = nullptr;
        if (!strip_prefix(__abs_path, &rest))
            return Status::Invalid;

        const bool want_write =
            (__flags & (bigos::vfs::OPEN_WRONLY | bigos::vfs::OPEN_RDWR | bigos::vfs::OPEN_TRUNC)) != 0;
        const char *root_cursor = rest;
        while (*root_cursor == '/')
            root_cursor++;
        if (*root_cursor == 0) {
            if (want_write || (__flags & bigos::vfs::OPEN_CREAT) != 0)
                return Status::IsDirectory;
            DiskInode root;
            if (!load_inode(ROOT_INODE, &root) || root.type != INODE_DIRECTORY)
                return Status::IoError;
            g_open_refs[ROOT_INODE]++;
            *__out_inode = ROOT_INODE;
            *__out_size = root.size;
            *__out_is_dir = true;
            return Status::Success;
        }

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
                discard_new_inode(new_inode);
                return add;
            }
            const Status commit = metadata_commit_file_create(parent, &dir, new_inode, &file);
            if (commit != Status::Success) {
            (void)dir_remove_entry(&dir, leaf);
                discard_new_inode(new_inode);
                return commit;
            }
            g_open_refs[new_inode]++;
            *__out_inode = new_inode;
            *__out_size = 0;
            *__out_is_dir = false;
            return Status::Success;
        }

        // Existing file.
        DiskInode file;
        if (!load_inode(inode_num, &file))
            return Status::IoError;
        if (file.type == INODE_DIRECTORY) {
            if (want_write || (__flags & bigos::vfs::OPEN_CREAT) != 0)
                return Status::IsDirectory;
            const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Read);
            if (acc != Status::Success)
                return acc;
            g_open_refs[inode_num]++;
            *__out_inode = inode_num;
            *__out_size = file.size;
            *__out_is_dir = true;
            return Status::Success;
        }
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
            const Status trunc_status = truncate(inode_num, 0, __uid, __gid);
            if (trunc_status != Status::Success)
                return trunc_status;
            if (!load_inode(inode_num, &file))
                return Status::IoError;
        }
        g_open_refs[inode_num]++;
        *__out_inode = inode_num;
        *__out_size = file.size;
        *__out_is_dir = false;
        return Status::Success;
    }

    void close_inode(uint32_t __inode) noexcept {
        if (!g_initialized || __inode >= INODE_COUNT)
            return;
        if (g_open_refs[__inode] > 0)
            g_open_refs[__inode]--;
        DiskInode node;
        if (!load_inode(__inode, &node))
            return;
        maybe_free_unlinked_inode(__inode, &node);
    }

    Status read(uint32_t __inode, uint64_t __offset, void *__dst, size_t __len, size_t *__out_read) noexcept {
        if (__out_read != nullptr)
            *__out_read = 0;
        if (!g_initialized)
            return Status::Invalid;
        if (__len == 0)
            return Status::Success;
        if (__dst == nullptr)
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
        if (!g_initialized)
            return Status::Invalid;
        if (__len == 0)
            return Status::Success;
        if (__src == nullptr)
            return Status::Invalid;
        if (__offset >= MAX_FILE_SIZE || (uint64_t)__len > MAX_FILE_SIZE - __offset)
            return Status::NoSpace;

        DiskInode file;
        if (!load_inode(__inode, &file))
            return Status::Invalid;
        if (file.type != INODE_REGULAR)
            return Status::IsDirectory;
        const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Write);
        if (acc != Status::Success)
            return acc;

        const uint64_t end = __offset + __len;
        const uint64_t writable = end - __offset;
        const uint32_t first_block = (uint32_t)(__offset / BLOCK_SIZE);
        const uint32_t last_block = (uint32_t)((end - 1) / BLOCK_SIZE);
        uint32_t missing_blocks = 0;
        for (uint32_t i = first_block; i <= last_block; i++) {
            if (i >= DIRECT_BLOCKS)
                return Status::NoSpace;
            if (file.direct[i] == 0)
                missing_blocks++;
        }
        if (missing_blocks != 0) {
            uint32_t free_blocks = 0;
            const Status count_status =
                bitmap_count_free(DATA_BITMAP_BLOCK, bigos::bigfs::DATA_BLOCK_COUNT, &free_blocks);
            if (count_status != Status::Success)
                return count_status;
            if (free_blocks < missing_blocks)
                return Status::NoSpace;
        }

        DiskInode committed = file;
        uint32_t allocated[DIRECT_BLOCKS] = {};
        uint32_t allocated_count = 0;
        for (uint32_t i = first_block; i <= last_block; i++) {
            if (file.direct[i] != 0)
                continue;
            uint32_t new_block = 0;
            const Status ab = reserve_data_block(&new_block);
            if (ab != Status::Success) {
                rollback_allocated_blocks(allocated, allocated_count);
                return ab;
            }
            committed.direct[i] = new_block;
            allocated[allocated_count++] = new_block;
        }
        if (end > committed.size)
            committed.size = end;

        uint32_t block_indices[DIRECT_BLOCKS] = {};
        bigos::bcache::BufferBlock *pinned[DIRECT_BLOCKS] = {};
        uint32_t block_count = 0;
        for (uint32_t i = first_block; i <= last_block; i++) {
            block_indices[block_count] = i;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, committed.direct[i]);
            if (block == nullptr) {
                put_pinned_blocks(pinned, block_count);
                rollback_allocated_blocks(allocated, allocated_count);
                return Status::IoError;
            }
            pinned[block_count] = block;
            if (file.direct[i] != 0)
                memcpy(g_write_staged[block_count], block->data, BLOCK_SIZE);
            else
                memset(g_write_staged[block_count], 0, BLOCK_SIZE);
            block_count++;
        }

        size_t done = 0;
        const uint8_t *in = (const uint8_t *)__src;
        while ((uint64_t)done < writable) {
            const uint32_t block_index = (uint32_t)((__offset + done) / BLOCK_SIZE);
            const uint32_t block_off = (uint32_t)((__offset + done) % BLOCK_SIZE);
            uint32_t chunk = BLOCK_SIZE - block_off;
            if ((uint64_t)chunk > writable - done)
                chunk = (uint32_t)(writable - done);

            uint32_t slot = 0;
            while (slot < block_count && block_indices[slot] != block_index)
                slot++;
            if (slot >= block_count) {
                put_pinned_blocks(pinned, block_count);
                rollback_allocated_blocks(allocated, allocated_count);
                return Status::IoError;
            }
            memcpy(g_write_staged[slot] + block_off, in + done, chunk);
            done += chunk;
        }

        const uint32_t inode_block_no = inode_table_block_no(__inode);
        const uint32_t inode_off = (__inode % INODES_PER_BLOCK) * INODE_SIZE;
        bigos::bcache::BufferBlock *inode_block = bigos::bcache::get(&g_device, inode_block_no);
        if (inode_block == nullptr) {
            put_pinned_blocks(pinned, block_count);
            rollback_allocated_blocks(allocated, allocated_count);
            return Status::IoError;
        }
        for (uint32_t i = 0; i < block_count; i++) {
            memcpy(pinned[i]->data, g_write_staged[i], BLOCK_SIZE);
            bigos::bcache::mark_dirty(pinned[i]);
        }
        memcpy(inode_block->data + inode_off, &committed, sizeof(DiskInode));
        bigos::bcache::mark_dirty(inode_block);
        bigos::bcache::put(inode_block);
        put_pinned_blocks(pinned, block_count);
        uint32_t data_blocks[DIRECT_BLOCKS] = {};
        for (uint32_t i = 0; i < block_count; i++)
            data_blocks[i] = committed.direct[block_indices[i]];
        const Status commit = metadata_commit_growth(__inode, &committed, data_blocks, block_count, allocated_count != 0);
        if (commit != Status::Success)
            return commit;
        if (__out_written != nullptr)
            *__out_written = done;
        return Status::Success;
    }

    Status truncate(uint32_t __inode, uint64_t __length, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized)
            return Status::Invalid;
        if (__length > MAX_FILE_SIZE)
            return Status::NoSpace;

        DiskInode file;
        if (!load_inode(__inode, &file))
            return Status::Invalid;
        if (file.type != INODE_REGULAR)
            return Status::IsDirectory;
        const Status acc = check_access(__uid, __gid, &file, bigos::cred::Access::Write);
        if (acc != Status::Success)
            return acc;
        if (__length == file.size)
            return Status::Success;

        DiskInode committed = file;
        const uint32_t keep_blocks = __length == 0 ? 0 : (uint32_t)((__length - 1) / BLOCK_SIZE) + 1;
        for (uint32_t i = keep_blocks; i < DIRECT_BLOCKS; i++)
            committed.direct[i] = 0;
        committed.size = __length;

        bigos::bcache::BufferBlock *tail_block = nullptr;
        uint8_t tail_staged[BLOCK_SIZE];
        bool zero_tail = false;
        if (__length < file.size && (__length % BLOCK_SIZE) != 0) {
            const uint32_t tail_index = (uint32_t)(__length / BLOCK_SIZE);
            if (tail_index < DIRECT_BLOCKS && file.direct[tail_index] != 0) {
                tail_block = bigos::bcache::get(&g_device, file.direct[tail_index]);
                if (tail_block == nullptr)
                    return Status::IoError;
                memcpy(tail_staged, tail_block->data, BLOCK_SIZE);
                memset(tail_staged + (__length % BLOCK_SIZE), 0, BLOCK_SIZE - (__length % BLOCK_SIZE));
                zero_tail = true;
            }
        }

        if (!store_inode(__inode, &committed)) {
            if (tail_block != nullptr)
                bigos::bcache::put(tail_block);
            return Status::IoError;
        }
        if (zero_tail) {
            memcpy(tail_block->data, tail_staged, BLOCK_SIZE);
            bigos::bcache::mark_dirty(tail_block);
        }
        if (tail_block != nullptr)
            bigos::bcache::put(tail_block);

        bool released_blocks = false;
        for (uint32_t i = keep_blocks; i < DIRECT_BLOCKS; i++) {
            if (file.direct[i] != 0) {
                released_blocks = true;
                free_data_block(file.direct[i]);
            }
        }
        const uint32_t tail_block_no = zero_tail ? file.direct[__length / BLOCK_SIZE] : 0;
        return metadata_commit_truncate(__inode, &committed, &file, tail_block_no, released_blocks);
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
            discard_new_inode(new_inode);
            return add;
        }
        const Status commit = metadata_commit_directory_create(parent, &dir, new_inode, &child);
        if (commit != Status::Success) {
            (void)dir_remove_entry(&dir, leaf);
            discard_new_inode(new_inode);
            return commit;
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

        const uint32_t old_link_count = tnode.link_count;
        tnode.link_count = 0;
        if (!store_inode(target, &tnode))
            return Status::IoError;
        if (!dir_remove_entry(&dir, leaf)) {
            tnode.link_count = old_link_count;
            (void)store_inode(target, &tnode);
            return Status::NotFound;
        }
        const DiskInode old_tnode = tnode;
        maybe_free_unlinked_inode(target, &tnode);
        return metadata_commit_unlink(parent, &dir, target, &tnode, &old_tnode, tnode.type == INODE_FREE);
    }

    Status rmdir(const char *__abs_path, uint32_t __uid, uint32_t __gid) noexcept {
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
        if (tnode.type == INODE_REGULAR)
            return Status::NotDirectory;
        if (tnode.type != INODE_DIRECTORY)
            return Status::Invalid;
        if (!dir_is_empty(&tnode))
            return Status::NotEmpty;

        const uint32_t old_link_count = tnode.link_count;
        tnode.link_count = 0;
        if (!store_inode(target, &tnode))
            return Status::IoError;
        if (!dir_remove_entry(&dir, leaf)) {
            tnode.link_count = old_link_count;
            (void)store_inode(target, &tnode);
            return Status::NotFound;
        }
        const DiskInode old_tnode = tnode;
        maybe_free_unlinked_inode(target, &tnode);
        return metadata_commit_unlink(parent, &dir, target, &tnode, &old_tnode, tnode.type == INODE_FREE);
    }

    Status rename(const char *__old_abs_path, const char *__new_abs_path, uint32_t __uid, uint32_t __gid) noexcept {
        if (!g_initialized || __old_abs_path == nullptr || __new_abs_path == nullptr)
            return Status::Invalid;
        const char *old_rest = nullptr;
        const char *new_rest = nullptr;
        if (!strip_prefix(__old_abs_path, &old_rest) || !strip_prefix(__new_abs_path, &new_rest))
            return Status::Invalid;

        uint32_t old_parent = 0;
        uint32_t new_parent = 0;
        char old_leaf[DIRENT_NAME_MAX + 1];
        char new_leaf[DIRENT_NAME_MAX + 1];
        Status status = resolve_parent(old_rest, &old_parent, old_leaf, sizeof(old_leaf));
        if (status != Status::Success)
            return status;
        status = resolve_parent(new_rest, &new_parent, new_leaf, sizeof(new_leaf));
        if (status != Status::Success)
            return status;

        if (old_parent == new_parent && str_eq(old_leaf, new_leaf))
            return Status::Success;

        DiskInode old_dir;
        DiskInode new_dir;
        if (!load_inode(old_parent, &old_dir) || old_dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;
        if (!load_inode(new_parent, &new_dir) || new_dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;

        uint32_t target = 0;
        if (!dir_lookup(&old_dir, old_leaf, &target))
            return Status::NotFound;
        uint32_t existing = 0;
        if (dir_lookup(&new_dir, new_leaf, &existing))
            return Status::Exists;

        const Status old_acc = check_access(__uid, __gid, &old_dir, bigos::cred::Access::Write);
        if (old_acc != Status::Success)
            return old_acc;
        if (new_parent != old_parent) {
            const Status new_acc = check_access(__uid, __gid, &new_dir, bigos::cred::Access::Write);
            if (new_acc != Status::Success)
                return new_acc;
        }

        DiskInode target_node;
        if (!load_inode(target, &target_node))
            return Status::IoError;
        if (target_node.type == INODE_DIRECTORY)
            return Status::IsDirectory;
        if (target_node.type != INODE_REGULAR)
            return Status::Invalid;

        if (old_parent == new_parent) {
            DirSlot src = {};
            if (!dir_find_slot(&old_dir, old_leaf, &src))
                return Status::NotFound;
            dirent_set(src.entry, new_leaf, target);
            bigos::bcache::mark_dirty(src.block);
            bigos::bcache::put(src.block);
            return metadata_commit_rename(old_parent, &old_dir, old_parent, &old_dir);
        }

        DirSlot src = {};
        if (!dir_find_slot(&old_dir, old_leaf, &src))
            return Status::NotFound;

        DirSlot dst = {};
        status = dir_reserve_slot(new_parent, &new_dir, &dst);
        if (status != Status::Success) {
            bigos::bcache::put(src.block);
            return status;
        }

        dirent_set(dst.entry, new_leaf, target);
        src.entry->inode_plus_one = 0;
        memset(src.entry->name, 0, sizeof(src.entry->name));
        bigos::bcache::mark_dirty(dst.block);
        bigos::bcache::mark_dirty(src.block);
        bigos::bcache::put(src.block);
        bigos::bcache::put(dst.block);
        return metadata_commit_rename(old_parent, &old_dir, new_parent, &new_dir);
    }

    Status readdir(uint32_t __inode, uint64_t __offset, DirectoryEntry *__entries, size_t __max_entries,
        size_t *__out_entries, uint64_t *__next_offset) noexcept {
        if (__out_entries != nullptr)
            *__out_entries = 0;
        if (__next_offset != nullptr)
            *__next_offset = __offset;
        if (!g_initialized || __entries == nullptr || __max_entries == 0 || __next_offset == nullptr)
            return Status::Invalid;
        if (__offset % DIRENT_SIZE != 0)
            return Status::Invalid;
        DiskInode dir;
        if (!load_inode(__inode, &dir))
            return Status::Invalid;
        if (dir.type != INODE_DIRECTORY)
            return Status::NotDirectory;

        const uint64_t max_slots = (uint64_t)DIRECT_BLOCKS * DIRENTS_PER_BLOCK;
        uint64_t slot = __offset / DIRENT_SIZE;
        size_t produced = 0;
        while (slot < max_slots && produced < __max_entries) {
            const uint32_t block_index = (uint32_t)(slot / DIRENTS_PER_BLOCK);
            const uint32_t entry_index = (uint32_t)(slot % DIRENTS_PER_BLOCK);
            slot++;
            const uint32_t block_no = dir.direct[block_index];
            if (block_no == 0)
                continue;
            bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, block_no);
            if (block == nullptr)
                return Status::IoError;
            DiskDirent *ent = (DiskDirent *)(block->data + entry_index * DIRENT_SIZE);
            if (ent->inode_plus_one != 0) {
                DiskInode child;
                const uint32_t child_inode = ent->inode_plus_one - 1;
                if (load_inode(child_inode, &child) && child.type != INODE_FREE) {
                    __entries[produced].type = child.type == INODE_DIRECTORY ? bigos::vfs::DIRENT_TYPE_DIRECTORY
                                                                             : bigos::vfs::DIRENT_TYPE_FILE;
                    memcpy(__entries[produced].name, ent->name, sizeof(__entries[produced].name));
                    __entries[produced].name[DIRENT_NAME_MAX] = 0;
                    produced++;
                }
            }
            bigos::bcache::put(block);
        }
        if (__out_entries != nullptr)
            *__out_entries = produced;
        *__next_offset = slot * DIRENT_SIZE;
        return Status::Success;
    }

    Status fsync() noexcept {
        if (!g_initialized)
            return Status::Invalid;
        if (g_metadata_commit.pending && !metadata_commit_flush())
            return Status::IoError;
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
