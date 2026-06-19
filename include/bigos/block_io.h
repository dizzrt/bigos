#ifndef _BIGOS_BLOCK_IO_H
#define _BIGOS_BLOCK_IO_H

#include <bigos/types.h>
#include <drivers/block/block_device.h>

NAMESPACE_BIGOS_BEG
namespace block_io {
    constexpr uint32_t QUEUE_CAPACITY_PER_DEVICE = 8;
    constexpr uint32_t MAX_DEVICE_QUEUES = 8;

    enum class Operation : uint32_t {
        Read = 0,
        Write,
    };

    enum class Status : uint32_t {
        Success = 0,
        InvalidRequest,
        QueueFull,
        DeviceNotReady,
        WouldBlock,
        BufferTooSmall,
        Overflow,
        Unsupported,
        DeviceTimeout,
        DeviceError,
        ShortRead,
    };

    struct Request {
        driver::block::BlockDevice *device;
        Operation operation;
        uint64_t lba;
        uint32_t sector_count;
        void *buffer;
        size_t buffer_len;
        Status status;
        uint32_t queue_slot;
        bool queued;
    };

    Status submit_sync(Request *__request) noexcept;
    Status read_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept;
    Status write_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count,
        const void *__src, size_t __src_len) noexcept;
    const char *status_name(Status __status) noexcept;
}   // namespace block_io
NAMESPACE_BIGOS_END

#endif   // _BIGOS_BLOCK_IO_H
