#ifndef _DRIVERS_RAM_BLOCK_DEVICE_H
#define _DRIVERS_RAM_BLOCK_DEVICE_H

#include <drivers/block/block_device.h>

NAMESPACE_DRIVER_BEG
namespace block {
    constexpr uint64_t RAM_BLOCK_DEFAULT_SECTORS = 64;

    struct RamBlockDevice {
        BlockDevice block;
        uint8_t *storage;
        uint64_t sector_count;
        uint32_t sector_size;
        bool initialized;
        bool fail_writes;
    };

    BlockStatus ram_block_init(
        RamBlockDevice *__device, uint8_t *__storage, uint64_t __sector_count, uint32_t __sector_size) noexcept;
    void ram_block_set_write_fault(RamBlockDevice *__device, bool __enabled) noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_RAM_BLOCK_DEVICE_H
