#ifndef _DRIVERS_BLOCK_DEVICE_H
#define _DRIVERS_BLOCK_DEVICE_H

#include <bigos/types.h>

namespace bigos::block_io {
    struct Request;
    struct CompletionToken;
    enum class Status : uint32_t;
}   // namespace bigos::block_io

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

    using WriteSectorsFn = BlockStatus (*)(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, const void *__src, size_t __src_len) noexcept;

    using IssueRequestFn = bigos::block_io::Status (*)(
        BlockDevice *__device, bigos::block_io::Request *__request,
        const bigos::block_io::CompletionToken *__token) noexcept;

    struct BlockDevice {
        uint32_t sector_size;
        // Zero means the device does not expose a reliable sector-count limit.
        uint64_t total_sectors;
        void *context;
        ReadSectorsFn read_impl;
        // Appended field (does not reorder the read-only layout above). A null
        // write_impl marks the device read-only; write_sectors() then returns
        // Unsupported (mapped to -EROFS by upper layers).
        WriteSectorsFn write_impl;
        // Optional nonpolling issue boundary. When present, block_io arms a
        // request-layer completion token before calling this function; the
        // backend must complete only that token through the request layer.
        IssueRequestFn issue_impl;
    };

    // Synchronous, read-only, non-IRQ-handler-safe block read contract.
    // The caller owns the destination buffer. This API validates range and
    // buffer bounds before dispatching to the hardware backend.
    BlockStatus read_sectors(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept;

    // Synchronous, non-IRQ-handler-safe block write contract symmetric to
    // read_sectors(). The caller owns the source buffer. This API validates the
    // sector count, source length and LBA-range overflow before dispatching to
    // the backend; a device with write_impl == nullptr is read-only and returns
    // Unsupported without issuing any device write.
    BlockStatus write_sectors(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, const void *__src, size_t __src_len) noexcept;

    const char *status_name(BlockStatus __status) noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_DEVICE_H
