#include <bigos/block_io.h>

#include <bigos/sched.h>

namespace {
    struct DeviceQueue {
        driver::block::BlockDevice *device;
        bigos::block_io::Request *slots[bigos::block_io::QUEUE_CAPACITY_PER_DEVICE];
        uint32_t depth;
    };

    DeviceQueue g_queues[bigos::block_io::MAX_DEVICE_QUEUES] = {};

    bool add_overflows_u64(uint64_t __a, uint64_t __b) noexcept {
        return __a > UINT64_MAX - __b;
    }

    bool mul_overflows_size(uint64_t __a, uint64_t __b, size_t *__out) noexcept {
        if (__out == nullptr)
            return true;
        if (__a != 0 && __b > UINT64_MAX / __a)
            return true;
        const uint64_t value = __a * __b;
        if (value > (uint64_t)SIZE_MAX)
            return true;
        *__out = (size_t)value;
        return false;
    }

    bigos::block_io::Status map_block_status(driver::block::BlockStatus __status) noexcept {
        using BlockStatus = driver::block::BlockStatus;
        using Status = bigos::block_io::Status;
        switch (__status) {
            case BlockStatus::Success:
                return Status::Success;
            case BlockStatus::InvalidArgument:
                return Status::InvalidRequest;
            case BlockStatus::BufferTooSmall:
                return Status::BufferTooSmall;
            case BlockStatus::Overflow:
                return Status::Overflow;
            case BlockStatus::Unsupported:
                return Status::Unsupported;
            case BlockStatus::DeviceTimeout:
                return Status::DeviceTimeout;
            case BlockStatus::DeviceError:
                return Status::DeviceError;
            case BlockStatus::ShortRead:
                return Status::ShortRead;
            default:
                return Status::DeviceError;
        }
    }

    bigos::block_io::Status map_device_status(bigos::device::Status __status) noexcept {
        using DeviceStatus = bigos::device::Status;
        using Status = bigos::block_io::Status;
        switch (__status) {
            case DeviceStatus::Success:
                return Status::Success;
            case DeviceStatus::InvalidArgument:
                return Status::InvalidRequest;
            case DeviceStatus::NotFound:
            case DeviceStatus::NotReady:
            case DeviceStatus::ProbeFailed:
                return Status::DeviceNotReady;
            case DeviceStatus::UnsupportedContext:
                return Status::WouldBlock;
            case DeviceStatus::Exists:
            case DeviceStatus::NoSpace:
            default:
                return Status::DeviceError;
        }
    }

    bigos::block_io::Status validate_request(const bigos::block_io::Request *__request) noexcept {
        using Operation = bigos::block_io::Operation;
        using Status = bigos::block_io::Status;
        if (__request == nullptr)
            return Status::InvalidRequest;
        driver::block::BlockDevice *device = __request->device;
        if (device == nullptr || device->sector_size == 0)
            return Status::InvalidRequest;
        if (__request->buffer == nullptr)
            return Status::InvalidRequest;
        if (__request->sector_count == 0)
            return Status::InvalidRequest;
        if (__request->operation != Operation::Read && __request->operation != Operation::Write)
            return Status::InvalidRequest;
        if (__request->operation == Operation::Read && device->read_impl == nullptr)
            return Status::DeviceNotReady;
        if (add_overflows_u64(__request->lba, __request->sector_count))
            return Status::Overflow;
        if (device->total_sectors != 0 && __request->lba + __request->sector_count > device->total_sectors)
            return Status::Overflow;

        size_t required = 0;
        if (mul_overflows_size(__request->sector_count, device->sector_size, &required))
            return Status::Overflow;
        if (__request->buffer_len < required)
            return Status::BufferTooSmall;
        return Status::Success;
    }

    DeviceQueue *queue_for(driver::block::BlockDevice *__device) noexcept {
        DeviceQueue *free_queue = nullptr;
        for (uint32_t i = 0; i < bigos::block_io::MAX_DEVICE_QUEUES; i++) {
            DeviceQueue *queue = &g_queues[i];
            if (queue->device == __device)
                return queue;
            if (queue->device == nullptr && free_queue == nullptr)
                free_queue = queue;
        }
        if (free_queue == nullptr)
            return nullptr;
        free_queue->device = __device;
        free_queue->depth = 0;
        for (uint32_t i = 0; i < bigos::block_io::QUEUE_CAPACITY_PER_DEVICE; i++)
            free_queue->slots[i] = nullptr;
        return free_queue;
    }

    bigos::block_io::Status dispatch_request(bigos::block_io::Request *__request) noexcept {
        using Operation = bigos::block_io::Operation;
        if (__request->operation == Operation::Read) {
            return map_block_status(driver::block::read_sectors(
                __request->device, __request->lba, __request->sector_count, __request->buffer, __request->buffer_len));
        }
        return map_block_status(driver::block::write_sectors(__request->device, __request->lba, __request->sector_count,
            __request->buffer, __request->buffer_len));
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace block_io {
    Status submit_sync(Request *__request) noexcept {
        const Status validation = validate_request(__request);
        if (__request != nullptr) {
            __request->status = validation;
            __request->queued = false;
            __request->queue_slot = UINT32_MAX;
        }
        if (validation != Status::Success)
            return validation;
        if (!bigos::sched::preemption_enabled()) {
            __request->status = Status::WouldBlock;
            return Status::WouldBlock;
        }

        DeviceQueue *queue = queue_for(__request->device);
        if (queue == nullptr || queue->depth >= QUEUE_CAPACITY_PER_DEVICE) {
            __request->status = Status::QueueFull;
            return Status::QueueFull;
        }

        const uint32_t slot = queue->depth++;
        queue->slots[slot] = __request;
        __request->queue_slot = slot;
        __request->queued = true;

        const Status status = dispatch_request(__request);
        __request->status = status;

        queue->slots[slot] = nullptr;
        queue->depth--;
        __request->queued = false;
        return status;
    }

    Status read_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept {
        Request request = {};
        request.device = __device;
        request.operation = Operation::Read;
        request.lba = __lba;
        request.sector_count = __sector_count;
        request.buffer = __dst;
        request.buffer_len = __dst_len;
        request.status = Status::InvalidRequest;
        request.queue_slot = UINT32_MAX;
        request.queued = false;
        return submit_sync(&request);
    }

    Status write_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count,
        const void *__src, size_t __src_len) noexcept {
        Request request = {};
        request.device = __device;
        request.operation = Operation::Write;
        request.lba = __lba;
        request.sector_count = __sector_count;
        request.buffer = const_cast<void *>(__src);
        request.buffer_len = __src_len;
        request.status = Status::InvalidRequest;
        request.queue_slot = UINT32_MAX;
        request.queued = false;
        return submit_sync(&request);
    }

    Status read_role_sync(device::DeviceRole __role, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept {
        const void *iface = nullptr;
        const bigos::device::Status status =
            bigos::device::find_interface(bigos::device::DeviceClass::Block, __role, &iface);
        if (status != bigos::device::Status::Success)
            return map_device_status(status);
        return read_sync((driver::block::BlockDevice *)iface, __lba, __sector_count, __dst, __dst_len);
    }

    Status write_role_sync(device::DeviceRole __role, uint64_t __lba, uint32_t __sector_count, const void *__src,
        size_t __src_len) noexcept {
        const void *iface = nullptr;
        const bigos::device::Status status =
            bigos::device::find_interface(bigos::device::DeviceClass::Block, __role, &iface);
        if (status != bigos::device::Status::Success)
            return map_device_status(status);
        return write_sync((driver::block::BlockDevice *)iface, __lba, __sector_count, __src, __src_len);
    }

    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Success:
                return "success";
            case Status::InvalidRequest:
                return "invalid-request";
            case Status::QueueFull:
                return "queue-full";
            case Status::DeviceNotReady:
                return "device-not-ready";
            case Status::WouldBlock:
                return "would-block";
            case Status::BufferTooSmall:
                return "buffer-too-small";
            case Status::Overflow:
                return "overflow";
            case Status::Unsupported:
                return "unsupported";
            case Status::DeviceTimeout:
                return "device-timeout";
            case Status::DeviceError:
                return "device-error";
            case Status::ShortRead:
                return "short-read";
            default:
                return "unknown";
        }
    }
}   // namespace block_io
NAMESPACE_BIGOS_END
