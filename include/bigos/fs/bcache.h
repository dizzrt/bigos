#ifndef _BIGOS_FS_BCACHE_H
#define _BIGOS_FS_BCACHE_H

#include <bigos/types.h>
#include <drivers/block/block_device.h>

NAMESPACE_BIGOS_BEG
namespace bcache {
    // One cache block maps exactly one device sector this stage (block size is a
    // fixed multiple of the sector size, fixed to 1 sector here so it aligns with
    // the existing read path). Bounded compile-time cache capacity.
    constexpr uint32_t BLOCK_SIZE = driver::block::DEFAULT_SECTOR_SIZE;
    constexpr uint32_t CACHE_BLOCKS = 32;

    enum class Status : int32_t {
        Success = 0,
        InvalidArgument,
        IoError,
        NoSpace,
    };

    // A single cached block keyed by (device, block_no). data points at a
    // kernel page region (the first BLOCK_SIZE bytes are used). valid means the
    // data mirrors the device; dirty means the cache holds unflushed writes;
    // ref_count > 0 pins the block against eviction. last_use is an approximate
    // LRU stamp updated on every get()/hit.
    struct BufferBlock {
        driver::block::BlockDevice *dev;
        uint64_t block_no;
        uint8_t *data;
        bool valid;
        bool dirty;
        bool used;
        uint32_t ref_count;
        uint64_t last_use;
    };

    // Lazily allocates the cache backing pages. Returns false on allocation
    // failure. Safe to call more than once; only the first call allocates.
    // Non-IRQ / blockable context only.
    bool init() noexcept;
    bool initialized() noexcept;

    // get() returns a pinned (ref_count incremented) block for (dev, block_no).
    // On a hit it returns the cached block without re-reading. On a miss it
    // selects a free or evictable slot, loads via block::read_sectors, marks it
    // valid and returns it. Eviction prefers an unreferenced clean block; the
    // only evictable candidate being dirty is written back first. When no slot
    // can be freed it returns nullptr (caller maps to -ENOMEM/-ENOSPC). Blockable
    // context only: it may allocate (during init) and perform synchronous block
    // IO. MUST NOT be called from IRQ / non-blockable context.
    BufferBlock *get(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept;

    // Releases one reference acquired by get(). Does not flush.
    void put(BufferBlock *__block) noexcept;

    // Marks a pinned block dirty so a later sync writes it back.
    void mark_dirty(BufferBlock *__block) noexcept;

    // Writes a dirty block back through block::write_sectors and clears dirty on
    // success. A clean block is a no-op success. On device error the block stays
    // dirty (no data loss) and IoError is returned. Blockable context only.
    Status sync(BufferBlock *__block) noexcept;

    // Writes a selected cached block identified by (device, block_no). Missing
    // or clean cache entries are success because there is no dirty selected
    // state to publish. On write-back error the cached block remains dirty.
    // Blockable context only.
    Status sync_block(driver::block::BlockDevice *__dev, uint64_t __block_no) noexcept;

    // Writes back every dirty cached block. Returns the first error encountered
    // (remaining dirty blocks keep their data). Blockable context only.
    Status sync_all() noexcept;

    // Writes back and drops every clean+dirty cached block that belongs to
    // __dev (used when tearing a RAM-backed device down or for smoke eviction).
    // Blocks still referenced are left in place. A dirty block that fails
    // write-back stays cached and dirty. Blockable context only.
    Status invalidate_device(driver::block::BlockDevice *__dev) noexcept;
}   // namespace bcache
NAMESPACE_BIGOS_END

#endif   // _BIGOS_FS_BCACHE_H
