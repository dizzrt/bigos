#ifndef _DRIVERS_BLOCK_DEVICE_H
#define _DRIVERS_BLOCK_DEVICE_H

#include <bigos/types.h>

NAMESPACE_DRIVER_BEG
namespace block {
    constexpr uint32_t DEFAULT_SECTOR_SIZE = 512;

    enum class BlockStatus : uint32_t {
        Success = 0,
        InvalidArgument,
        BufferTooSmall,
        Overflow,
        Unsupported,
        DeviceTimeout,
        DeviceError,
        ShortRead,
    };

    struct BlockDevice;

    using ReadSectorsFn = BlockStatus (*)(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept;

    struct BlockDevice {
        uint32_t sector_size;
        // Zero means the device does not expose a reliable sector-count limit.
        uint64_t total_sectors;
        void *context;
        ReadSectorsFn read_impl;
    };

    // Synchronous, read-only, non-IRQ-handler-safe block read contract.
    // The caller owns the destination buffer. This API validates range and
    // buffer bounds before dispatching to the hardware backend.
    BlockStatus read_sectors(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept;

    const char *status_name(BlockStatus __status) noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_DEVICE_H
