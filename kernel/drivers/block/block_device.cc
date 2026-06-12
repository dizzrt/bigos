#include <drivers/block/block_device.h>

namespace {
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
}   // namespace

NAMESPACE_DRIVER_BEG
namespace block {
    BlockStatus read_sectors(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
        if (__device == nullptr || __device->read_impl == nullptr || __device->sector_size == 0)
            return BlockStatus::InvalidArgument;
        if (__dst == nullptr)
            return BlockStatus::InvalidArgument;
        if (__sector_count == 0)
            return BlockStatus::InvalidArgument;
        if (add_overflows_u64(__lba, __sector_count))
            return BlockStatus::Overflow;
        if (__device->total_sectors != 0 && __lba + __sector_count > __device->total_sectors)
            return BlockStatus::Overflow;

        size_t required = 0;
        if (mul_overflows_size(__sector_count, __device->sector_size, &required))
            return BlockStatus::Overflow;
        if (__dst_len < required)
            return BlockStatus::BufferTooSmall;

        return __device->read_impl(__device, __lba, __sector_count, __dst, __dst_len);
    }

    BlockStatus write_sectors(
        BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, const void *__src, size_t __src_len) noexcept {
        if (__device == nullptr || __device->sector_size == 0)
            return BlockStatus::InvalidArgument;
        if (__src == nullptr)
            return BlockStatus::InvalidArgument;
        if (__sector_count == 0)
            return BlockStatus::InvalidArgument;
        // A read-only device exposes no write backend.
        if (__device->write_impl == nullptr)
            return BlockStatus::Unsupported;
        if (add_overflows_u64(__lba, __sector_count))
            return BlockStatus::Overflow;
        if (__device->total_sectors != 0 && __lba + __sector_count > __device->total_sectors)
            return BlockStatus::Overflow;

        size_t required = 0;
        if (mul_overflows_size(__sector_count, __device->sector_size, &required))
            return BlockStatus::Overflow;
        if (__src_len < required)
            return BlockStatus::BufferTooSmall;

        return __device->write_impl(__device, __lba, __sector_count, __src, __src_len);
    }

    const char *status_name(BlockStatus __status) noexcept {
        switch (__status) {
            case BlockStatus::Success:
                return "success";
            case BlockStatus::InvalidArgument:
                return "invalid-argument";
            case BlockStatus::BufferTooSmall:
                return "buffer-too-small";
            case BlockStatus::Overflow:
                return "overflow";
            case BlockStatus::Unsupported:
                return "unsupported";
            case BlockStatus::DeviceTimeout:
                return "device-timeout";
            case BlockStatus::DeviceError:
                return "device-error";
            case BlockStatus::ShortRead:
                return "short-read";
            default:
                return "unknown";
        }
    }
}   // namespace block
NAMESPACE_DRIVER_END
