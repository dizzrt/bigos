#include <drivers/block/ram_block_device.h>

#include <string.h>

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

    driver::block::BlockStatus validate_ram_range(const driver::block::RamBlockDevice *__ram, uint64_t __lba,
        uint32_t __sector_count, size_t __buffer_len, size_t *__bytes) noexcept {
        if (__ram == nullptr || !__ram->initialized || __ram->storage == nullptr || __ram->sector_size == 0)
            return driver::block::BlockStatus::InvalidArgument;
        if (__sector_count == 0)
            return driver::block::BlockStatus::InvalidArgument;
        if (add_overflows_u64(__lba, __sector_count))
            return driver::block::BlockStatus::Overflow;
        if (__lba + __sector_count > __ram->sector_count)
            return driver::block::BlockStatus::Overflow;
        size_t bytes = 0;
        if (mul_overflows_size(__sector_count, __ram->sector_size, &bytes))
            return driver::block::BlockStatus::Overflow;
        if (__buffer_len < bytes)
            return driver::block::BlockStatus::BufferTooSmall;
        if (__bytes != nullptr)
            *__bytes = bytes;
        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus ram_block_read_impl(driver::block::BlockDevice *__device, uint64_t __lba,
        uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
        if (__device == nullptr || __device->context == nullptr || __dst == nullptr)
            return driver::block::BlockStatus::InvalidArgument;
        driver::block::RamBlockDevice *ram = (driver::block::RamBlockDevice *)__device->context;
        size_t bytes = 0;
        const driver::block::BlockStatus status =
            validate_ram_range(ram, __lba, __sector_count, __dst_len, &bytes);
        if (status != driver::block::BlockStatus::Success)
            return status;
        memcpy(__dst, ram->storage + __lba * ram->sector_size, bytes);
        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus ram_block_write_impl(driver::block::BlockDevice *__device, uint64_t __lba,
        uint32_t __sector_count, const void *__src, size_t __src_len) noexcept {
        if (__device == nullptr || __device->context == nullptr || __src == nullptr)
            return driver::block::BlockStatus::InvalidArgument;
        driver::block::RamBlockDevice *ram = (driver::block::RamBlockDevice *)__device->context;
        size_t bytes = 0;
        const driver::block::BlockStatus status =
            validate_ram_range(ram, __lba, __sector_count, __src_len, &bytes);
        if (status != driver::block::BlockStatus::Success)
            return status;
        if (ram->fail_writes)
            return driver::block::BlockStatus::DeviceError;
        memcpy(ram->storage + __lba * ram->sector_size, __src, bytes);
        return driver::block::BlockStatus::Success;
    }
}   // namespace

NAMESPACE_DRIVER_BEG
namespace block {
    BlockStatus ram_block_init(
        RamBlockDevice *__device, uint8_t *__storage, uint64_t __sector_count, uint32_t __sector_size) noexcept {
        if (__device == nullptr || __storage == nullptr || __sector_count == 0)
            return BlockStatus::InvalidArgument;
        if (__sector_size != DEFAULT_SECTOR_SIZE)
            return BlockStatus::Unsupported;

        size_t total_bytes = 0;
        if (mul_overflows_size(__sector_count, __sector_size, &total_bytes))
            return BlockStatus::Overflow;

        memset(__storage, 0, total_bytes);
        __device->storage = __storage;
        __device->sector_count = __sector_count;
        __device->sector_size = __sector_size;
        __device->initialized = true;
        __device->fail_writes = false;
        __device->block.sector_size = __sector_size;
        __device->block.total_sectors = __sector_count;
        __device->block.context = __device;
        __device->block.read_impl = &ram_block_read_impl;
        __device->block.write_impl = &ram_block_write_impl;
        return BlockStatus::Success;
    }

    void ram_block_set_write_fault(RamBlockDevice *__device, bool __enabled) noexcept {
        if (__device == nullptr)
            return;
        __device->fail_writes = __enabled;
    }
}   // namespace block
NAMESPACE_DRIVER_END
