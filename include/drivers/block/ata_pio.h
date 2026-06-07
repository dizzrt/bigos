#ifndef _DRIVERS_BLOCK_ATA_PIO_H
#define _DRIVERS_BLOCK_ATA_PIO_H

#include <drivers/block/block_device.h>

NAMESPACE_DRIVER_BEG
namespace block {
    struct AtaPioDevice {
        BlockDevice block;
    };

    // Bochs-focused ATA PIO backend: primary master, LBA48 READ SECTORS EXT,
    // synchronous polling, 512-byte sectors, no DMA/AHCI/NVMe, and not IRQ-safe.
    void ata_pio_primary_master_init(AtaPioDevice *__device) noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_ATA_PIO_H
