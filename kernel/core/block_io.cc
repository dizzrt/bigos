#include <bigos/block_io.h>

#include <bigos/sched.h>
#include <irq/interrupt.h>

namespace {
    struct DeviceQueue {
        driver::block::BlockDevice *device;
        bigos::block_io::Request *slots[bigos::block_io::QUEUE_CAPACITY_PER_DEVICE];
        uint32_t generations[bigos::block_io::QUEUE_CAPACITY_PER_DEVICE];
        uint32_t active_count;
        volatile uint32_t lock;
    };

    DeviceQueue g_queues[bigos::block_io::MAX_DEVICE_QUEUES] = {};
    volatile uint32_t g_queues_lock = 0;
    constexpr bigos::timer::tick_t DEFAULT_SUBMIT_TIMEOUT_TICKS = 200;

    void spin_lock(volatile uint32_t *__lock) noexcept {
        while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
            while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
                asm volatile("pause" ::: "memory");
        }
    }

    void spin_unlock(volatile uint32_t *__lock) noexcept {
        __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
    }

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
        if (__request->operation == Operation::Write && device->write_impl == nullptr)
            return Status::Unsupported;
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
        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&g_queues_lock);
        DeviceQueue *free_queue = nullptr;
        for (uint32_t i = 0; i < bigos::block_io::MAX_DEVICE_QUEUES; i++) {
            DeviceQueue *queue = &g_queues[i];
            if (queue->device == __device) {
                spin_unlock(&g_queues_lock);
                return queue;
            }
            if (queue->device == nullptr && free_queue == nullptr)
                free_queue = queue;
        }
        if (free_queue == nullptr) {
            spin_unlock(&g_queues_lock);
            return nullptr;
        }
        free_queue->device = __device;
        free_queue->active_count = 0;
        free_queue->lock = 0;
        for (uint32_t i = 0; i < bigos::block_io::QUEUE_CAPACITY_PER_DEVICE; i++) {
            free_queue->slots[i] = nullptr;
            free_queue->generations[i] = 0;
        }
        spin_unlock(&g_queues_lock);
        return free_queue;
    }

    DeviceQueue *queue_lookup(driver::block::BlockDevice *__device) noexcept {
        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&g_queues_lock);
        for (uint32_t i = 0; i < bigos::block_io::MAX_DEVICE_QUEUES; i++) {
            DeviceQueue *queue = &g_queues[i];
            if (queue->device == __device) {
                spin_unlock(&g_queues_lock);
                return queue;
            }
        }
        spin_unlock(&g_queues_lock);
        return nullptr;
    }

    void prepare_request_state(bigos::block_io::Request *__request, bigos::block_io::Status __status) noexcept {
        if (__request == nullptr)
            return;
        __request->status = __status;
        __request->state = __status == bigos::block_io::Status::Success ? bigos::block_io::RequestState::Queued
                                                                        : bigos::block_io::RequestState::Invalid;
        __request->completion = {};
        bigos::sched::init_wait_queue(&__request->completion_wait);
        __request->queue_slot = UINT32_MAX;
        __request->completion_generation = 0;
        __request->queued = false;
        __request->wake_issued = false;
    }

    bool terminal_status_is_success(bigos::block_io::Status __status) noexcept {
        return __status == bigos::block_io::Status::Success;
    }

    bool completion_status_allowed(bigos::block_io::Status __status) noexcept {
        using Status = bigos::block_io::Status;
        return __status == Status::Success || __status == Status::InvalidRequest ||
               __status == Status::DeviceNotReady || __status == Status::BufferTooSmall ||
               __status == Status::Overflow || __status == Status::Unsupported || __status == Status::DeviceTimeout ||
               __status == Status::DeviceError || __status == Status::ShortRead;
    }

    void set_terminal_state(bigos::block_io::Request *__request, bigos::block_io::Status __status) noexcept {
        __request->status = __status;
        if (__status == bigos::block_io::Status::PendingTimeout)
            __request->state = bigos::block_io::RequestState::TimeoutOrCancelled;
        else
            __request->state = terminal_status_is_success(__status) ? bigos::block_io::RequestState::CompletedSuccess
                                                                    : bigos::block_io::RequestState::CompletedError;
    }

    bool completion_done_predicate(void *__arg) noexcept {
        const bigos::block_io::Request *request = (const bigos::block_io::Request *)__arg;
        return request == nullptr || request->state == bigos::block_io::RequestState::CompletedSuccess ||
               request->state == bigos::block_io::RequestState::CompletedError ||
               request->state == bigos::block_io::RequestState::TimeoutOrCancelled;
    }

    bigos::block_io::Status enqueue_request(DeviceQueue *__queue, bigos::block_io::Request *__request,
        bigos::block_io::CompletionToken *__out_token, bigos::block_io::RequestState __state) noexcept {
        if (__queue == nullptr || __request == nullptr)
            return bigos::block_io::Status::InvalidRequest;
        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&__queue->lock);
        if (__queue->active_count >= bigos::block_io::QUEUE_CAPACITY_PER_DEVICE) {
            spin_unlock(&__queue->lock);
            return bigos::block_io::Status::QueueFull;
        }
        uint32_t slot = UINT32_MAX;
        for (uint32_t i = 0; i < bigos::block_io::QUEUE_CAPACITY_PER_DEVICE; i++) {
            if (__queue->slots[i] == nullptr) {
                slot = i;
                break;
            }
        }
        if (slot == UINT32_MAX) {
            spin_unlock(&__queue->lock);
            return bigos::block_io::Status::QueueFull;
        }
        uint32_t generation = ++__queue->generations[slot];
        if (generation == 0) {
            __queue->generations[slot] = 1;
            generation = 1;
        }
        __queue->slots[slot] = __request;
        __queue->active_count++;
        __request->queue_slot = slot;
        __request->completion_generation = generation;
        __request->queued = true;
        __request->state = __state;
        __request->completion = {__request, __request->device, slot, generation};
        if (__out_token != nullptr)
            *__out_token = __request->completion;
        spin_unlock(&__queue->lock);
        return bigos::block_io::Status::Success;
    }

    void release_request_slot_locked(DeviceQueue *__queue, bigos::block_io::Request *__request) noexcept {
        if (__queue == nullptr || __request == nullptr || !__request->queued)
            return;
        const uint32_t slot = __request->queue_slot;
        if (slot < bigos::block_io::QUEUE_CAPACITY_PER_DEVICE && __queue->slots[slot] == __request) {
            __queue->slots[slot] = nullptr;
            if (__queue->active_count > 0)
                __queue->active_count--;
        }
        __request->queued = false;
        __request->queue_slot = UINT32_MAX;
    }

    void release_request_slot(DeviceQueue *__queue, bigos::block_io::Request *__request) noexcept {
        if (__queue == nullptr || __request == nullptr || !__request->queued)
            return;
        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&__queue->lock);
        release_request_slot_locked(__queue, __request);
        spin_unlock(&__queue->lock);
    }

    bigos::block_io::Status dispatch_request(bigos::block_io::Request *__request) noexcept {
        using Operation = bigos::block_io::Operation;
        if (__request->operation == Operation::Read) {
            return map_block_status(driver::block::read_sectors(
                __request->device, __request->lba, __request->sector_count, __request->buffer, __request->buffer_len));
        }
        return map_block_status(driver::block::write_sectors(
            __request->device, __request->lba, __request->sector_count, __request->buffer, __request->buffer_len));
    }

    bigos::block_io::Status issue_request(
        bigos::block_io::Request *__request, const bigos::block_io::CompletionToken *__token) noexcept {
        if (__request == nullptr || __request->device == nullptr || __token == nullptr)
            return bigos::block_io::Status::InvalidRequest;
        if (__request->device->issue_impl != nullptr)
            return __request->device->issue_impl(__request->device, __request, __token);

        const bigos::block_io::Status final_status = dispatch_request(__request);
        const bigos::block_io::Status completion_status = bigos::block_io::complete_from_irq(__token, final_status);
        return completion_status == bigos::block_io::Status::Success ? bigos::block_io::Status::Success
                                                                     : completion_status;
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace block_io {
    Status submit_sync(Request *__request) noexcept {
        const Status validation = validate_request(__request);
        prepare_request_state(__request, validation);
        if (validation != Status::Success)
            return validation;
        if (!bigos::sched::can_block()) {
            __request->status = Status::WouldBlock;
            __request->state = RequestState::Invalid;
            return Status::WouldBlock;
        }

        DeviceQueue *queue = queue_for(__request->device);
        CompletionToken token = {};
        const Status enqueue_status = enqueue_request(queue, __request, &token, RequestState::Pending);
        if (enqueue_status != Status::Success) {
            __request->status = enqueue_status;
            __request->state = RequestState::Invalid;
            return enqueue_status;
        }
        __request->status = Status::WouldBlock;

        const Status issue_status = issue_request(__request, &token);
        if (issue_status != Status::Success) {
            bigos::irq::InterruptGuard irq_guard;
            spin_lock(&queue->lock);
            if (__request->state == RequestState::Pending) {
                set_terminal_state(__request, issue_status);
                release_request_slot_locked(queue, __request);
            }
            spin_unlock(&queue->lock);
            return __request->status;
        }

        return wait_pending(__request, DEFAULT_SUBMIT_TIMEOUT_TICKS);
    }

    Status arm_pending(Request *__request, CompletionToken *__out_token) noexcept {
        const Status validation = validate_request(__request);
        prepare_request_state(__request, validation);
        if (__out_token != nullptr)
            *__out_token = {};
        if (validation != Status::Success)
            return validation;
        if (!bigos::sched::can_block()) {
            __request->status = Status::WouldBlock;
            __request->state = RequestState::Invalid;
            return Status::WouldBlock;
        }
        DeviceQueue *queue = queue_for(__request->device);
        const Status enqueue_status = enqueue_request(queue, __request, __out_token, RequestState::Pending);
        if (enqueue_status != Status::Success) {
            __request->status = enqueue_status;
            __request->state = RequestState::Invalid;
            return enqueue_status;
        }
        __request->status = Status::WouldBlock;
        return Status::Success;
    }

    Status wait_pending(Request *__request, timer::tick_t __timeout_ticks) noexcept {
        if (__request == nullptr)
            return Status::InvalidRequest;
        if (__request->state == RequestState::CompletedSuccess || __request->state == RequestState::CompletedError ||
            __request->state == RequestState::TimeoutOrCancelled) {
            release_request_slot(queue_for(__request->device), __request);
            return __request->status;
        }
        if (__request->state != RequestState::Pending)
            return Status::InvalidRequest;
        if (!bigos::sched::can_block())
            return Status::WouldBlock;
        const int wait_status = bigos::sched::wait_queue_wait_until(
            &__request->completion_wait, &completion_done_predicate, __request, __timeout_ticks);
        if (wait_status == bigos::sched::WAIT_BLOCK_FORBIDDEN)
            return Status::WouldBlock;
        if (wait_status == bigos::sched::WAIT_INVALID)
            return Status::InvalidRequest;

        DeviceQueue *queue = queue_for(__request->device);
        if (wait_status == bigos::sched::WAIT_TIMEOUT) {
            bigos::irq::InterruptGuard irq_guard;
            spin_lock(&queue->lock);
            if (__request->state == RequestState::Pending) {
                set_terminal_state(__request, Status::PendingTimeout);
                release_request_slot_locked(queue, __request);
            }
            spin_unlock(&queue->lock);
            return __request->status;
        }

        if (__request->state == RequestState::Pending)
            return Status::WouldBlock;
        release_request_slot(queue, __request);
        return __request->status;
    }

    Status cancel_pending(Request *__request) noexcept {
        if (__request == nullptr)
            return Status::InvalidRequest;
        DeviceQueue *queue = queue_for(__request->device);
        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&queue->lock);
        if (__request->state != RequestState::Pending) {
            spin_unlock(&queue->lock);
            return Status::CompletionRejected;
        }
        set_terminal_state(__request, Status::PendingTimeout);
        release_request_slot_locked(queue, __request);
        if (!__request->wake_issued) {
            __request->wake_issued = true;
            spin_unlock(&queue->lock);
            bigos::sched::wake_all(&__request->completion_wait);
        } else {
            spin_unlock(&queue->lock);
        }
        return Status::Success;
    }

    Status complete_from_irq(const CompletionToken *__token, Status __final_status) noexcept {
        if (__token == nullptr || __token->request == nullptr || __token->device == nullptr)
            return Status::CompletionRejected;
        if (!completion_status_allowed(__final_status))
            return Status::CompletionRejected;
        DeviceQueue *queue = queue_lookup(__token->device);
        if (queue == nullptr)
            return Status::CompletionRejected;

        Request *request = __token->request;
        bool wake = false;
        {
            bigos::irq::InterruptGuard irq_guard;
            spin_lock(&queue->lock);
            const uint32_t slot = __token->queue_slot;
            if (slot >= QUEUE_CAPACITY_PER_DEVICE || queue->slots[slot] != request ||
                queue->generations[slot] != __token->generation || request->device != __token->device ||
                request->completion_generation != __token->generation || request->state != RequestState::Pending) {
                spin_unlock(&queue->lock);
                return Status::CompletionRejected;
            }
            set_terminal_state(request, __final_status);
            if (!request->wake_issued) {
                request->wake_issued = true;
                wake = true;
            }
            spin_unlock(&queue->lock);
        }
        if (wake)
            (void)bigos::sched::wake_all(&request->completion_wait);
        return Status::Success;
    }

    Status read_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst,
        size_t __dst_len) noexcept {
        if (__device != nullptr && __device->issue_impl != nullptr && __sector_count > 1) {
            if (__dst == nullptr || __device->sector_size == 0)
                return Status::InvalidRequest;
            if (add_overflows_u64(__lba, __sector_count))
                return Status::Overflow;
            size_t required = 0;
            if (mul_overflows_size(__sector_count, __device->sector_size, &required))
                return Status::Overflow;
            if (__dst_len < required)
                return Status::BufferTooSmall;
            uint8_t *dst = (uint8_t *)__dst;
            for (uint32_t sector = 0; sector < __sector_count; sector++) {
                const Status status = read_sync(
                    __device, __lba + sector, 1, dst + (size_t)sector * __device->sector_size, __device->sector_size);
                if (status != Status::Success)
                    return status;
            }
            return Status::Success;
        }

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

    Status write_sync(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, const void *__src,
        size_t __src_len) noexcept {
        if (__device != nullptr && __device->issue_impl != nullptr && __sector_count > 1) {
            if (__src == nullptr || __device->sector_size == 0)
                return Status::InvalidRequest;
            if (add_overflows_u64(__lba, __sector_count))
                return Status::Overflow;
            size_t required = 0;
            if (mul_overflows_size(__sector_count, __device->sector_size, &required))
                return Status::Overflow;
            if (__src_len < required)
                return Status::BufferTooSmall;
            const uint8_t *src = (const uint8_t *)__src;
            for (uint32_t sector = 0; sector < __sector_count; sector++) {
                const Status status = write_sync(
                    __device, __lba + sector, 1, src + (size_t)sector * __device->sector_size, __device->sector_size);
                if (status != Status::Success)
                    return status;
            }
            return Status::Success;
        }

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

    Status read_role_sync(
        device::DeviceRole __role, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
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
            case Status::PendingTimeout:
                return "pending-timeout";
            case Status::CompletionRejected:
                return "completion-rejected";
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

    const char *request_state_name(RequestState __state) noexcept {
        switch (__state) {
            case RequestState::Invalid:
                return "invalid";
            case RequestState::Queued:
                return "queued";
            case RequestState::Pending:
                return "pending";
            case RequestState::CompletedSuccess:
                return "completed-success";
            case RequestState::CompletedError:
                return "completed-error";
            case RequestState::TimeoutOrCancelled:
                return "timeout-or-cancelled";
            default:
                return "unknown";
        }
    }
}   // namespace block_io
NAMESPACE_BIGOS_END
