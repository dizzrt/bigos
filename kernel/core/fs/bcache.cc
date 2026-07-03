#include <bigos/fs/bcache.h>

#include <bigos/block_io.h>
#include <bigos/memory.h>
#include <bigos/sched.h>

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
    constexpr uint32_t PROTECTED_BLOCKS_MAX = 64;
    struct ProtectedBlock {
        driver::block::BlockDevice *dev;
        uint64_t block_no;
        bool used;
    };
    ProtectedBlock g_protected[PROTECTED_BLOCKS_MAX] = {};

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
        if (!bigos::sched::can_block() || !bigos::sched::preemption_enabled())
            return bigos::bcache::Status::WouldBlock;
        const bigos::block_io::Status status = bigos::block_io::write_sync(
            __slot->dev, __slot->block_no, 1, __slot->data, bigos::bcache::BLOCK_SIZE);
        if (status != bigos::block_io::Status::Success)
            return bigos::bcache::Status::IoError;
        __slot->dirty = false;
        return bigos::bcache::Status::Success;
    }

    bool is_protected(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept {
        for (uint32_t i = 0; i < PROTECTED_BLOCKS_MAX; i++) {
            if (g_protected[i].used && g_protected[i].dev == __dev && g_protected[i].block_no == __block_no)
                return true;
        }
        return false;
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
        for (uint32_t i = 0; i < PROTECTED_BLOCKS_MAX; i++) {
            g_protected[i].dev = nullptr;
            g_protected[i].block_no = 0;
            g_protected[i].used = false;
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
        if (!bigos::sched::can_block() || !bigos::sched::preemption_enabled())
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
                if (slot->dirty && is_protected(slot->dev, slot->block_no))
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
        const bigos::block_io::Status read_status =
            bigos::block_io::read_sync(__dev, __block_no, 1, victim->data, BLOCK_SIZE);
        if (read_status != bigos::block_io::Status::Success)
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

    Status sync_block(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept {
        if (__dev == nullptr)
            return Status::InvalidArgument;
        BufferBlock *slot = find_slot(__dev, __block_no);
        if (slot == nullptr || !slot->used || !slot->dirty)
            return Status::Success;
        return write_back(slot);
    }

    Status ordered_flush(const OrderedFlushPlan *__plan) noexcept {
        if (__plan == nullptr || __plan->dev == nullptr || __plan->phase_count > ORDERED_FLUSH_PHASES_MAX)
            return Status::InvalidArgument;
        if (!bigos::sched::can_block() || !bigos::sched::preemption_enabled())
            return Status::WouldBlock;
        for (uint32_t p = 0; p < __plan->phase_count; p++) {
            const OrderedFlushPhase *phase = &__plan->phases[p];
            if (phase->count > ORDERED_FLUSH_BLOCKS_MAX)
                return Status::InvalidArgument;
            for (uint32_t i = 0; i < phase->count; i++) {
                const Status status = sync_block(__plan->dev, phase->blocks[i]);
                if (status != Status::Success)
                    return status;
            }
        }
        return Status::Success;
    }

    Status protect_blocks(driver::block::BlockDevice *__dev, const uint64_t *__blocks, uint32_t __count) noexcept {
        if (__dev == nullptr || (__count != 0 && __blocks == nullptr))
            return Status::InvalidArgument;
        for (uint32_t i = 0; i < __count; i++) {
            if (is_protected(__dev, __blocks[i]))
                continue;
            bool inserted = false;
            for (uint32_t p = 0; p < PROTECTED_BLOCKS_MAX; p++) {
                if (g_protected[p].used)
                    continue;
                g_protected[p].dev = __dev;
                g_protected[p].block_no = __blocks[i];
                g_protected[p].used = true;
                inserted = true;
                break;
            }
            if (!inserted)
                return Status::NoSpace;
        }
        return Status::Success;
    }

    void unprotect_blocks(driver::block::BlockDevice *__dev, const uint64_t *__blocks, uint32_t __count) noexcept {
        if (__dev == nullptr || (__count != 0 && __blocks == nullptr))
            return;
        for (uint32_t i = 0; i < __count; i++) {
            for (uint32_t p = 0; p < PROTECTED_BLOCKS_MAX; p++) {
                if (!g_protected[p].used || g_protected[p].dev != __dev || g_protected[p].block_no != __blocks[i])
                    continue;
                g_protected[p].dev = nullptr;
                g_protected[p].block_no = 0;
                g_protected[p].used = false;
                break;
            }
        }
    }

    Status sync_device(driver::block::BlockDevice *__dev) noexcept {
        if (__dev == nullptr)
            return Status::InvalidArgument;
        Status first_error = Status::Success;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            BufferBlock *slot = &g_slots[i];
            if (!slot->used || slot->dev != __dev || !slot->dirty)
                continue;
            if (is_protected(slot->dev, slot->block_no)) {
                if (first_error == Status::Success)
                    first_error = Status::IoError;
                continue;
            }
            const Status status = write_back(slot);
            if (status != Status::Success && first_error == Status::Success)
                first_error = status;
        }
        return first_error;
    }

    Status sync_all() noexcept {
        Status first_error = Status::Success;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            BufferBlock *slot = &g_slots[i];
            if (!slot->used || !slot->dirty)
                continue;
            if (is_protected(slot->dev, slot->block_no)) {
                if (first_error == Status::Success)
                    first_error = Status::IoError;
                continue;
            }
            const Status status = write_back(slot);
            if (status != Status::Success && first_error == Status::Success)
                first_error = status;
        }
        return first_error;
    }

    Status invalidate_device(driver::block::BlockDevice *__dev) noexcept {
        if (__dev == nullptr)
            return Status::InvalidArgument;
        Status first_error = Status::Success;
        for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
            BufferBlock *slot = &g_slots[i];
            if (!slot->used || slot->dev != __dev || slot->ref_count != 0)
                continue;
            if (slot->dirty) {
                if (is_protected(slot->dev, slot->block_no)) {
                    if (first_error == Status::Success)
                        first_error = Status::IoError;
                    continue;
                }
                const Status status = write_back(slot);
                if (status != Status::Success) {
                    if (first_error == Status::Success)
                        first_error = status;
                    continue;
                }
            }
            slot->dev = nullptr;
            slot->block_no = 0;
            slot->valid = false;
            slot->dirty = false;
            slot->used = false;
            slot->last_use = 0;
        }
        return first_error;
    }
}   // namespace bcache
NAMESPACE_BIGOS_END
