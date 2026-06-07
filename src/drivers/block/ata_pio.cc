#include <bigos/io.h>
#include <drivers/block/ata_pio.h>

namespace {
    constexpr uint16_t ATA_DATA = 0x01f0;
    constexpr uint16_t ATA_SECTOR_COUNT = 0x01f2;
    constexpr uint16_t ATA_LBA_LOW = 0x01f3;
    constexpr uint16_t ATA_LBA_MID = 0x01f4;
    constexpr uint16_t ATA_LBA_HIGH = 0x01f5;
    constexpr uint16_t ATA_DRIVE_HEAD = 0x01f6;
    constexpr uint16_t ATA_COMMAND_STATUS = 0x01f7;

    constexpr uint8_t ATA_PRIMARY_MASTER = 0x40;
    constexpr uint8_t ATA_CMD_READ_SECTORS_EXT = 0x24;
    constexpr uint8_t ATA_STATUS_BSY = 0x80;
    constexpr uint8_t ATA_STATUS_DRQ = 0x08;
    constexpr uint8_t ATA_STATUS_DF = 0x20;
    constexpr uint8_t ATA_STATUS_ERR = 0x01;
    constexpr uint32_t ATA_POLL_LIMIT = 1000000;
    constexpr uint32_t ATA_MAX_SECTORS_PER_CMD = 65535;

    driver::block::BlockStatus wait_for_data() noexcept {
        for (uint32_t retry = 0; retry < ATA_POLL_LIMIT; retry++) {
            asm volatile("pause");
            const uint8_t status = bigos::inb(ATA_COMMAND_STATUS);
            if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0)
                return driver::block::BlockStatus::DeviceError;
            if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ)
                return driver::block::BlockStatus::Success;
        }
        return driver::block::BlockStatus::DeviceTimeout;
    }

    driver::block::BlockStatus ata_read_impl(
        driver::block::BlockDevice *, uint64_t __lba, uint32_t __sector_count, void *__dst, size_t __dst_len) noexcept {
        if (__sector_count == 0 || __sector_count > ATA_MAX_SECTORS_PER_CMD)
            return driver::block::BlockStatus::Unsupported;
        if (__lba > 0x0000FFFFFFFFFFFFull)
            return driver::block::BlockStatus::Unsupported;
        if (__dst_len < (size_t)__sector_count * driver::block::DEFAULT_SECTOR_SIZE)
            return driver::block::BlockStatus::BufferTooSmall;

        const uint8_t lba1 = (uint8_t)(__lba & 0xffu);
        const uint8_t lba2 = (uint8_t)((__lba >> 8) & 0xffu);
        const uint8_t lba3 = (uint8_t)((__lba >> 16) & 0xffu);
        const uint8_t lba4 = (uint8_t)((__lba >> 24) & 0xffu);
        const uint8_t lba5 = (uint8_t)((__lba >> 32) & 0xffu);
        const uint8_t lba6 = (uint8_t)((__lba >> 40) & 0xffu);

        bigos::outb(ATA_DRIVE_HEAD, ATA_PRIMARY_MASTER);
        bigos::outb(ATA_SECTOR_COUNT, (uint8_t)(__sector_count >> 8));
        bigos::outb(ATA_LBA_LOW, lba4);
        bigos::outb(ATA_LBA_MID, lba5);
        bigos::outb(ATA_LBA_HIGH, lba6);
        bigos::outb(ATA_SECTOR_COUNT, (uint8_t)__sector_count);
        bigos::outb(ATA_LBA_LOW, lba1);
        bigos::outb(ATA_LBA_MID, lba2);
        bigos::outb(ATA_LBA_HIGH, lba3);
        bigos::outb(ATA_COMMAND_STATUS, ATA_CMD_READ_SECTORS_EXT);

        uint16_t *out = (uint16_t *)__dst;
        for (uint32_t sector = 0; sector < __sector_count; sector++) {
            const driver::block::BlockStatus wait_status = wait_for_data();
            if (wait_status != driver::block::BlockStatus::Success)
                return wait_status;
            for (uint32_t word = 0; word < driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t); word++)
                out[sector * (driver::block::DEFAULT_SECTOR_SIZE / sizeof(uint16_t)) + word] = bigos::inw(ATA_DATA);
        }

        return driver::block::BlockStatus::Success;
    }
}   // namespace

NAMESPACE_DRIVER_BEG
namespace block {
    void ata_pio_primary_master_init(AtaPioDevice *__device) noexcept {
        if (__device == nullptr)
            return;
        __device->block.sector_size = DEFAULT_SECTOR_SIZE;
        __device->block.total_sectors = 0;
        __device->block.context = __device;
        __device->block.read_impl = ata_read_impl;
    }
}   // namespace block
NAMESPACE_DRIVER_END
