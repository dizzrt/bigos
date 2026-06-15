#ifndef _DRIVERS_BLOCK_ATA_PIO_H
#define _DRIVERS_BLOCK_ATA_PIO_H

#include <drivers/block/block_device.h>

NAMESPACE_DRIVER_BEG
namespace block {
    struct AtaPioDevice {
        BlockDevice block;
        uint16_t io_base;
        uint16_t control_base;
        uint8_t drive_head;
    };

    // Bochs-focused ATA PIO backend: primary master, LBA48 READ SECTORS EXT,
    // synchronous polling, 512-byte sectors, no DMA/AHCI/NVMe, and not IRQ-safe.
    void ata_pio_primary_master_init(AtaPioDevice *__device) noexcept;
    // Same bounded PIO backend for an independent persistent /rw test disk on a
    // helper-owned ISA IDE controller. It stays off the default BIOS/PIIX boot
    // channels so it does not affect drive 0x80 boot discovery.
    void ata_pio_persistent_test_init(AtaPioDevice *__device) noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_ATA_PIO_H
