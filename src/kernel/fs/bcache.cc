#include <bigos/fs/bcache.h>

#include <bigos/memory.h>

// Internal allocator flag: alloc_kernel_pages() only returns mapped, accessible
// backing when pre-paging is requested. The cache data pages are touched
// immediately, so request pre-paged kernel pages.
#include "../../mm/memdef.h"

namespace {
    // Backing storage for the bounded cache. The slots array and the data pages
    // are allocated once on first init(). Each slot's data points at a
    // BLOCK_SIZE-aligned region carved out of the page allocation.
    bigos::bcache::BufferBlock g_slots[bigos::bcache::CACHE_BLOCKS] = {};
    uint8_t *g_data = nullptr;
    bool g_initialized = false;
    uint64_t g_clock = 0;

    uint64_t next_stamp() noexcept {
        return ++g_clock;
    }

    bigos::bcache::BufferBlock *find_slot(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept {
        for (uint32_t i = 0; i < bigos::bcache::CACHE_BLOCKS; i++) {
            bigos::bcache::BufferBlock *slot = &g_slots[i];
            if (slot->used && slot->dev == __dev && slot->block_no == __block_no)
                return slot;
        }
        return nullptr;
    }

    bigos::bcache::Status write_back(bigos::bcache::BufferBlock *__slot) noexcept {
        const driver::block::BlockStatus status = driver::block::write_sectors(
            __slot->dev, __slot->block_no, 1, __slot->data, bigos::bcache::BLOCK_SIZE);
        if (status != driver::block::BlockStatus::Success)
            return bigos::bcache::Status::IoError;
        __slot->dirty = false;
        return bigos::bcache::Status::Success;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace bcache {
    bool init() noexcept {
        if (g_initialized)
            return true;
        // Carve CACHE_BLOCKS * BLOCK_SIZE bytes from page-granular storage.
        const uint64_t total_bytes = (uint64_t)CACHE_BLOCKS * BLOCK_SIZE;
        const uint32_t pages = (uint32_t)((total_bytes + 0xfff) / 0x1000);
        uint8_t *data = (uint8_t *)bigos::alloc_kernel_pages(pages, _GFM_PRE_PAGING);
        if (data == nullptr)
            return false;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            g_slots[i].dev = nullptr;
            g_slots[i].block_no = 0;
            g_slots[i].data = data + (uint64_t)i * BLOCK_SIZE;
            g_slots[i].valid = false;
            g_slots[i].dirty = false;
            g_slots[i].used = false;
            g_slots[i].ref_count = 0;
            g_slots[i].last_use = 0;
        }
        g_data = data;
        g_initialized = true;
        return true;
    }

    bool initialized() noexcept {
        return g_initialized;
    }

    BufferBlock *get(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept {
        if (__dev == nullptr)
            return nullptr;
        if (!g_initialized && !init())
            return nullptr;

        // Hit: return the pinned cached block without re-reading.
        BufferBlock *hit = find_slot(__dev, __block_no);
        if (hit != nullptr) {
            hit->ref_count++;
            hit->last_use = next_stamp();
            return hit;
        }

        // Miss: prefer a never-used free slot.
        BufferBlock *victim = nullptr;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            if (!g_slots[i].used) {
                victim = &g_slots[i];
                break;
            }
        }

        // No free slot: pick the least-recently-used unreferenced clean block.
        if (victim == nullptr) {
            BufferBlock *clean_lru = nullptr;
            for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
                BufferBlock *slot = &g_slots[i];
                if (slot->ref_count != 0 || slot->dirty)
                    continue;
                if (clean_lru == nullptr || slot->last_use < clean_lru->last_use)
                    clean_lru = slot;
            }
            victim = clean_lru;
        }

        // Still nothing: the only evictable candidate is a dirty unreferenced
        // block. Write it back first, then reuse its slot.
        if (victim == nullptr) {
            BufferBlock *dirty_lru = nullptr;
            for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
                BufferBlock *slot = &g_slots[i];
                if (slot->ref_count != 0)
                    continue;
                if (dirty_lru == nullptr || slot->last_use < dirty_lru->last_use)
                    dirty_lru = slot;
            }
            if (dirty_lru == nullptr)
                return nullptr;   // every slot is pinned -> deterministic failure
            if (write_back(dirty_lru) != Status::Success)
                return nullptr;   // keep the block dirty, do not lose data
            victim = dirty_lru;
        }

        // Load the requested block into the victim slot.
        const driver::block::BlockStatus read_status =
            driver::block::read_sectors(__dev, __block_no, 1, victim->data, BLOCK_SIZE);
        if (read_status != driver::block::BlockStatus::Success)
            return nullptr;

        victim->dev = __dev;
        victim->block_no = __block_no;
        victim->valid = true;
        victim->dirty = false;
        victim->used = true;
        victim->ref_count = 1;
        victim->last_use = next_stamp();
        return victim;
    }

    void put(BufferBlock *__block) noexcept {
        if (__block == nullptr || __block->ref_count == 0)
            return;
        __block->ref_count--;
    }

    void mark_dirty(BufferBlock *__block) noexcept {
        if (__block == nullptr || !__block->used)
            return;
        __block->dirty = true;
    }

    Status sync(BufferBlock *__block) noexcept {
        if (__block == nullptr || !__block->used)
            return Status::InvalidArgument;
        if (!__block->dirty)
            return Status::Success;
        return write_back(__block);
    }

    Status sync_all() noexcept {
        Status first_error = Status::Success;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            BufferBlock *slot = &g_slots[i];
            if (!slot->used || !slot->dirty)
                continue;
            const Status status = write_back(slot);
            if (status != Status::Success && first_error == Status::Success)
                first_error = status;
        }
        return first_error;
    }

    void invalidate_device(driver::block::BlockDevice *__dev) noexcept {
        if (__dev == nullptr)
            return;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            BufferBlock *slot = &g_slots[i];
            if (!slot->used || slot->dev != __dev || slot->ref_count != 0)
                continue;
            if (slot->dirty)
                (void)write_back(slot);
            slot->dev = nullptr;
            slot->block_no = 0;
            slot->valid = false;
            slot->dirty = false;
            slot->used = false;
            slot->last_use = 0;
        }
    }
}   // namespace bcache
NAMESPACE_BIGOS_END
