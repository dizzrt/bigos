#include <bigos/io.h>
#include <drivers/block/ata_pio.h>
#include <irq/interrupt.h>

namespace {
    constexpr uint16_t ATA_PRIMARY_IO_BASE = 0x01f0;
    constexpr uint16_t ATA_PRIMARY_CONTROL_BASE = 0x03f6;
    constexpr uint16_t ATA_SECONDARY_IO_BASE = 0x0170;
    constexpr uint16_t ATA_SECONDARY_CONTROL_BASE = 0x0376;
    constexpr uint16_t ATA_PERSISTENT_TEST_IO_BASE = 0x0168;
    constexpr uint16_t ATA_PERSISTENT_TEST_CONTROL_BASE = 0x036e;
    constexpr uint16_t ATA_DATA_OFFSET = 0;
    constexpr uint16_t ATA_SECTOR_COUNT_OFFSET = 2;
    constexpr uint16_t ATA_LBA_LOW_OFFSET = 3;
    constexpr uint16_t ATA_LBA_MID_OFFSET = 4;
    constexpr uint16_t ATA_LBA_HIGH_OFFSET = 5;
    constexpr uint16_t ATA_DRIVE_HEAD_OFFSET = 6;
    constexpr uint16_t ATA_COMMAND_STATUS_OFFSET = 7;

    constexpr uint8_t ATA_MASTER = 0x40;
    constexpr uint8_t ATA_CMD_READ_SECTORS_EXT = 0x24;
    constexpr uint8_t ATA_CMD_WRITE_SECTORS_EXT = 0x34;
    constexpr uint8_t ATA_CMD_FLUSH_CACHE_EXT = 0xEA;
    constexpr uint8_t ATA_STATUS_BSY = 0x80;
    constexpr uint8_t ATA_STATUS_DRQ = 0x08;
    constexpr uint8_t ATA_STATUS_DF = 0x20;
    constexpr uint8_t ATA_STATUS_ERR = 0x01;
    constexpr uint32_t ATA_POLL_LIMIT = 1000000;
    constexpr uint32_t ATA_MAX_SECTORS_PER_CMD = 65535;
    driver::block::AtaPioDevice *g_primary_irq_device = nullptr;

    void spin_lock(volatile uint32_t *__lock) noexcept {
        while (__atomic_exchange_n(__lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
            while (__atomic_load_n(__lock, __ATOMIC_RELAXED) != 0u)
                asm volatile("pause" ::: "memory");
        }
    }

    void spin_unlock(volatile uint32_t *__lock) noexcept {
        __atomic_store_n(__lock, 0u, __ATOMIC_RELEASE);
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

    uint16_t io_base_for(driver::block::BlockDevice *__device) noexcept {
        if (__device == nullptr || __device->context == nullptr)
            return ATA_PRIMARY_IO_BASE;
        const driver::block::AtaPioDevice *ata = (const driver::block::AtaPioDevice *)__device->context;
        return ata->io_base;
    }

    uint16_t port_for(driver::block::BlockDevice *__device, uint16_t __offset) noexcept {
        return (uint16_t)(io_base_for(__device) + __offset);
    }

    uint16_t port_for_ata(const driver::block::AtaPioDevice *__device, uint16_t __offset) noexcept {
        return (uint16_t)((__device != nullptr ? __device->io_base : ATA_PRIMARY_IO_BASE) + __offset);
    }

    void ata_read_sector(driver::block::AtaPioDevice *__ata, uint32_t __sector_index) noexcept {
        uint16_t *out = (uint16_t *)(__ata->rw_buffer +
                                     (size_t)__sector_index * driver::block::DEFAULT_SECTOR_SIZE);
        for (uint32_t word = 0; word < driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t); word++)
            out[word] = bigos::inw(port_for_ata(__ata, ATA_DATA_OFFSET));
    }

    void ata_write_sector(driver::block::AtaPioDevice *__ata, uint32_t __sector_index) noexcept {
        const uint16_t *in = (const uint16_t *)(__ata->rw_buffer +
                                               (size_t)__sector_index * driver::block::DEFAULT_SECTOR_SIZE);
        for (uint32_t word = 0; word < driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t); word++)
            bigos::outw(port_for_ata(__ata, ATA_DATA_OFFSET), in[word]);
    }

    driver::block::BlockStatus wait_for_data(driver::block::BlockDevice *__device) noexcept {
        for (uint32_t retry = 0; retry < ATA_POLL_LIMIT; retry++) {
            asm volatile("pause");
            const uint8_t status = bigos::inb(port_for(__device, ATA_COMMAND_STATUS_OFFSET));
            if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0)
                return driver::block::BlockStatus::DeviceError;
            if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ)
                return driver::block::BlockStatus::Success;
        }
        return driver::block::BlockStatus::DeviceTimeout;
    }

    // Wait until the device is no longer busy and reports no error/fault. Used
    // after the WRITE SECTORS data phase and the FLUSH CACHE command, where the
    // device clears BSY without necessarily asserting DRQ.
    driver::block::BlockStatus wait_not_busy(driver::block::BlockDevice *__device) noexcept {
        for (uint32_t retry = 0; retry < ATA_POLL_LIMIT; retry++) {
            asm volatile("pause");
            const uint8_t status = bigos::inb(port_for(__device, ATA_COMMAND_STATUS_OFFSET));
            if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0)
                return driver::block::BlockStatus::DeviceError;
            if ((status & ATA_STATUS_BSY) == 0)
                return driver::block::BlockStatus::Success;
        }
        return driver::block::BlockStatus::DeviceTimeout;
    }

    driver::block::BlockStatus wait_drive_selected(driver::block::BlockDevice *__device) noexcept {
        for (uint32_t retry = 0; retry < ATA_POLL_LIMIT; retry++) {
            asm volatile("pause");
            const uint8_t status = bigos::inb(port_for(__device, ATA_COMMAND_STATUS_OFFSET));
            if ((status & ATA_STATUS_BSY) == 0)
                return driver::block::BlockStatus::Success;
        }
        return driver::block::BlockStatus::DeviceTimeout;
    }

    uint8_t drive_head_for(driver::block::BlockDevice *__device) noexcept {
        if (__device == nullptr || __device->context == nullptr)
            return ATA_MASTER;
        const driver::block::AtaPioDevice *ata = (const driver::block::AtaPioDevice *)__device->context;
        return ata->drive_head;
    }

    driver::block::BlockStatus ata_program_lba(
        driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count) noexcept {
        const uint8_t lba1 = (uint8_t)(__lba & 0xffu);
        const uint8_t lba2 = (uint8_t)((__lba >> 8) & 0xffu);
        const uint8_t lba3 = (uint8_t)((__lba >> 16) & 0xffu);
        const uint8_t lba4 = (uint8_t)((__lba >> 24) & 0xffu);
        const uint8_t lba5 = (uint8_t)((__lba >> 32) & 0xffu);
        const uint8_t lba6 = (uint8_t)((__lba >> 40) & 0xffu);

        bigos::outb(port_for(__device, ATA_DRIVE_HEAD_OFFSET), drive_head_for(__device));
        const driver::block::BlockStatus select_status = wait_drive_selected(__device);
        if (select_status != driver::block::BlockStatus::Success)
            return select_status;
        bigos::outb(port_for(__device, ATA_SECTOR_COUNT_OFFSET), (uint8_t)(__sector_count >> 8));
        bigos::outb(port_for(__device, ATA_LBA_LOW_OFFSET), lba4);
        bigos::outb(port_for(__device, ATA_LBA_MID_OFFSET), lba5);
        bigos::outb(port_for(__device, ATA_LBA_HIGH_OFFSET), lba6);
        bigos::outb(port_for(__device, ATA_SECTOR_COUNT_OFFSET), (uint8_t)__sector_count);
        bigos::outb(port_for(__device, ATA_LBA_LOW_OFFSET), lba1);
        bigos::outb(port_for(__device, ATA_LBA_MID_OFFSET), lba2);
        bigos::outb(port_for(__device, ATA_LBA_HIGH_OFFSET), lba3);
        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus ata_read_impl(
        driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
        if (__sector_count == 0 || __sector_count > ATA_MAX_SECTORS_PER_CMD)
            return driver::block::BlockStatus::Unsupported;
        if (__lba > 0x0000FFFFFFFFFFFFull)
            return driver::block::BlockStatus::Unsupported;
        if (__dst_len < (size_t)__sector_count * driver::block::DEFAULT_SECTOR_SIZE)
            return driver::block::BlockStatus::BufferTooSmall;

        const driver::block::BlockStatus program_status = ata_program_lba(__device, __lba, __sector_count);
        if (program_status != driver::block::BlockStatus::Success)
            return program_status;
        bigos::outb(port_for(__device, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_READ_SECTORS_EXT);

        uint16_t *out = (uint16_t *)__dst;
        for (uint32_t sector = 0; sector < __sector_count; sector++) {
            const driver::block::BlockStatus wait_status = wait_for_data(__device);
            if (wait_status != driver::block::BlockStatus::Success)
                return wait_status;
            for (uint32_t word = 0; word < driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t); word++)
                out[sector * (driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t)) + word] =
                    bigos::inw(port_for(__device, ATA_DATA_OFFSET));
        }

        return driver::block::BlockStatus::Success;
    }

    driver::block::BlockStatus ata_write_impl(driver::block::BlockDevice *__device, uint64_t __lba, uint32_t __sector_count,
        const void *__src, size_t __src_len) noexcept {
        if (__sector_count == 0 || __sector_count > ATA_MAX_SECTORS_PER_CMD)
            return driver::block::BlockStatus::Unsupported;
        if (__lba > 0x0000FFFFFFFFFFFFull)
            return driver::block::BlockStatus::Unsupported;
        if (__src_len < (size_t)__sector_count * driver::block::DEFAULT_SECTOR_SIZE)
            return driver::block::BlockStatus::BufferTooSmall;

        const driver::block::BlockStatus program_status = ata_program_lba(__device, __lba, __sector_count);
        if (program_status != driver::block::BlockStatus::Success)
            return program_status;
        bigos::outb(port_for(__device, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_WRITE_SECTORS_EXT);

        const uint16_t *in = (const uint16_t *)__src;
        for (uint32_t sector = 0; sector < __sector_count; sector++) {
            const driver::block::BlockStatus wait_status = wait_for_data(__device);
            if (wait_status != driver::block::BlockStatus::Success)
                return wait_status;
            for (uint32_t word = 0; word < driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t); word++)
                bigos::outw(
                    port_for(__device, ATA_DATA_OFFSET),
                    in[sector * (driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t)) + word]);
            // The device asserts BSY while it commits the sector; wait for it to
            // clear before pushing the next sector's data phase.
            const driver::block::BlockStatus drain_status = wait_not_busy(__device);
            if (drain_status != driver::block::BlockStatus::Success)
                return drain_status;
        }

        // Flush the device write cache so the write is durable before return.
        bigos::outb(port_for(__device, ATA_DRIVE_HEAD_OFFSET), drive_head_for(__device));
        bigos::outb(port_for(__device, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_FLUSH_CACHE_EXT);
        return wait_not_busy(__device);
    }

    void ata_reset_active_locked(driver::block::AtaPioDevice *__ata) noexcept {
        __ata->active = false;
        __ata->phase = driver::block::AtaPioPhase::Idle;
        __ata->active_request = nullptr;
        __ata->active_token = {};
        __ata->next_sector = 0;
        __ata->sectors_total = 0;
        __ata->rw_buffer = nullptr;
    }

    bigos::block_io::Status ata_finish_from_irq_locked(
        driver::block::AtaPioDevice *__ata, bigos::block_io::Status __status,
        bigos::block_io::CompletionToken *__out_token) noexcept {
        if (__ata == nullptr || !__ata->active || __out_token == nullptr)
            return bigos::block_io::Status::CompletionRejected;
        *__out_token = __ata->active_token;
        ata_reset_active_locked(__ata);
        return __status;
    }

    bigos::block_io::Status ata_issue_irq(driver::block::BlockDevice *__device, bigos::block_io::Request *__request,
        const bigos::block_io::CompletionToken *__token) noexcept {
        if (__device == nullptr || __device->context == nullptr || __request == nullptr || __token == nullptr)
            return bigos::block_io::Status::InvalidRequest;
        driver::block::AtaPioDevice *ata = (driver::block::AtaPioDevice *)__device->context;
        if (!ata->irq_completion_enabled)
            return bigos::block_io::Status::Unsupported;
        if (__request->sector_count != 1)
            return bigos::block_io::Status::Unsupported;
        if (__request->lba > 0x0000FFFFFFFFFFFFull)
            return bigos::block_io::Status::Unsupported;

        bigos::irq::InterruptGuard irq_guard;
        spin_lock(&ata->lock);
        if (ata->active) {
            spin_unlock(&ata->lock);
            return bigos::block_io::Status::QueueFull;
        }

        const driver::block::BlockStatus program_status =
            ata_program_lba(__device, __request->lba, __request->sector_count);
        if (program_status != driver::block::BlockStatus::Success) {
            spin_unlock(&ata->lock);
            return map_block_status(program_status);
        }

        ata->active = true;
        ata->active_request = __request;
        ata->active_token = *__token;
        ata->next_sector = 0;
        ata->sectors_total = __request->sector_count;
        ata->rw_buffer = (uint8_t *)__request->buffer;

        if (__request->operation == bigos::block_io::Operation::Read) {
            ata->phase = driver::block::AtaPioPhase::Reading;
            bigos::outb(port_for(__device, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_READ_SECTORS_EXT);
            spin_unlock(&ata->lock);
            return bigos::block_io::Status::Success;
        }

        ata->phase = driver::block::AtaPioPhase::Writing;
        bigos::outb(port_for(__device, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_WRITE_SECTORS_EXT);
        const driver::block::BlockStatus data_status = wait_for_data(__device);
        if (data_status != driver::block::BlockStatus::Success) {
            ata_reset_active_locked(ata);
            spin_unlock(&ata->lock);
            return map_block_status(data_status);
        }
        ata_write_sector(ata, 0);
        ata->next_sector = 1;
        spin_unlock(&ata->lock);
        return bigos::block_io::Status::Success;
    }

    void ata_pio_init_drive(
        driver::block::AtaPioDevice *__device, uint16_t __io_base, uint16_t __control_base, uint8_t __drive_head,
        bool __irq_completion) noexcept {
        if (__device == nullptr)
            return;
        __device->io_base = __io_base;
        __device->control_base = __control_base;
        __device->drive_head = __drive_head;
        __device->lock = 0;
        __device->irq_completion_enabled = __irq_completion;
        ata_reset_active_locked(__device);
        __device->block.sector_size = driver::block::DEFAULT_SECTOR_SIZE;
        __device->block.total_sectors = 0;
        __device->block.context = __device;
        __device->block.read_impl = ata_read_impl;
        __device->block.write_impl = ata_write_impl;
        __device->block.issue_impl = __irq_completion ? ata_issue_irq : nullptr;
        if (__irq_completion)
            bigos::outb(__control_base, 0x00);
    }
}   // namespace

NAMESPACE_DRIVER_BEG
namespace block {
    void ata_pio_primary_master_init(AtaPioDevice *__device) noexcept {
        ata_pio_init_drive(__device, ATA_PRIMARY_IO_BASE, ATA_PRIMARY_CONTROL_BASE, ATA_MASTER, true);
        g_primary_irq_device = __device;
    }

    void ata_pio_persistent_test_init(AtaPioDevice *__device) noexcept {
        ata_pio_init_drive(__device, ATA_PERSISTENT_TEST_IO_BASE, ATA_PERSISTENT_TEST_CONTROL_BASE, ATA_MASTER, false);
    }

    void ata_pio_primary_irq() noexcept {
        AtaPioDevice *ata = g_primary_irq_device;
        if (ata == nullptr)
            return;

        bigos::block_io::CompletionToken token = {};
        bigos::block_io::Status final_status = bigos::block_io::Status::CompletionRejected;
        bool complete = false;

        spin_lock(&ata->lock);
        const uint8_t status = bigos::inb(port_for_ata(ata, ATA_COMMAND_STATUS_OFFSET));
        if (!ata->active) {
            spin_unlock(&ata->lock);
            return;
        }
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0) {
            final_status = ata_finish_from_irq_locked(ata, bigos::block_io::Status::DeviceError, &token);
            complete = true;
        } else if (ata->phase == AtaPioPhase::Reading) {
            if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ) {
                ata_read_sector(ata, ata->next_sector);
                ata->next_sector++;
                if (ata->next_sector >= ata->sectors_total) {
                    final_status = ata_finish_from_irq_locked(ata, bigos::block_io::Status::Success, &token);
                    complete = true;
                }
            } else if ((status & ATA_STATUS_BSY) == 0) {
                final_status = ata_finish_from_irq_locked(ata, bigos::block_io::Status::DeviceError, &token);
                complete = true;
            }
        } else if (ata->phase == AtaPioPhase::Writing) {
            if (ata->next_sector < ata->sectors_total) {
                if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ) {
                    ata_write_sector(ata, ata->next_sector);
                    ata->next_sector++;
                } else if ((status & ATA_STATUS_BSY) == 0) {
                    final_status = ata_finish_from_irq_locked(ata, bigos::block_io::Status::DeviceError, &token);
                    complete = true;
                }
            }
            if (!complete && ata->next_sector >= ata->sectors_total) {
                ata->phase = AtaPioPhase::Flushing;
                bigos::outb(port_for_ata(ata, ATA_DRIVE_HEAD_OFFSET), ata->drive_head);
                bigos::outb(port_for_ata(ata, ATA_COMMAND_STATUS_OFFSET), ATA_CMD_FLUSH_CACHE_EXT);
            }
        } else if (ata->phase == AtaPioPhase::Flushing) {
            if ((status & ATA_STATUS_BSY) == 0) {
                final_status = ata_finish_from_irq_locked(ata, bigos::block_io::Status::Success, &token);
                complete = true;
            }
        }
        spin_unlock(&ata->lock);

        if (complete)
            (void)bigos::block_io::complete_from_irq(&token, final_status);
    }
}   // namespace block
NAMESPACE_DRIVER_END
