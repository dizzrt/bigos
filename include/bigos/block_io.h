#ifndef _BIGOS_BLOCK_IO_H
#define _BIGOS_BLOCK_IO_H

#include <bigos/types.h>
#include <bigos/device.h>
#include <bigos/sched.h>
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
        PendingTimeout,
        CompletionRejected,
        BufferTooSmall,
        Overflow,
        Unsupported,
        DeviceTimeout,
        DeviceError,
        ShortRead,
    };

    enum class RequestState : uint32_t {
        Invalid = 0,
        Queued,
        Pending,
        CompletedSuccess,
        CompletedError,
        TimeoutOrCancelled,
    };

    struct Request;

    struct CompletionToken {
        Request *request;
        driver::block::BlockDevice *device;
        uint32_t queue_slot;
        uint32_t generation;
    };

    struct Request {
        driver::block::BlockDevice *device;
        Operation operation;
        uint64_t lba;
        uint32_t sector_count;
        void *buffer;
        size_t buffer_len;
        Status status;
        RequestState state;
        CompletionToken completion;
        sched::WaitQueue completion_wait;
        uint32_t queue_slot;
        uint32_t completion_generation;
        bool queued;
        bool wake_issued;
    };

    Status submit_sync(Request *__request) noexcept;
    Status arm_pending(Request *__request, CompletionToken *__out_token) noexcept;
    Status wait_pending(Request *__request, timer::tick_t __timeout_ticks = 0) noexcept;
    Status cancel_pending(Request *__request) noexcept;
    Status complete_from_irq(const CompletionToken *__token, Status __final_status) noexcept;
    Status read_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept;
    Status write_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count,
        const void *__src, size_t __src_len) noexcept;
    Status read_role_sync(device::DeviceRole __role, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept;
    Status write_role_sync(device::DeviceRole __role, uint64_t __lba, uint32_t __sector_count, const void *__src,
        size_t __src_len) noexcept;
    const char *status_name(Status __status) noexcept;
    const char *request_state_name(RequestState __state) noexcept;
}   // namespace block_io
NAMESPACE_BIGOS_END

#endif   // _BIGOS_BLOCK_IO_H
