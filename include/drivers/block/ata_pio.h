#ifndef _DRIVERS_BLOCK_ATA_PIO_H
#define _DRIVERS_BLOCK_ATA_PIO_H

#include <bigos/block_io.h>
#include <drivers/block/block_device.h>

NAMESPACE_DRIVER_BEG
namespace block {
    enum class AtaPioPhase : uint8_t {
        Idle = 0,
        Reading,
        Writing,
        Flushing,
    };

    struct AtaPioDevice {
        BlockDevice block;
        uint16_t io_base;
        uint16_t control_base;
        uint8_t drive_head;
        volatile uint32_t lock;
        bool irq_completion_enabled;
        bool active;
        AtaPioPhase phase;
        bigos::block_io::Request *active_request;
        bigos::block_io::CompletionToken active_token;
        uint32_t next_sector;
        uint32_t sectors_total;
        uint8_t *rw_buffer;
    };

    // Bochs/QEMU-focused ATA PIO backend: primary master, LBA48 READ/WRITE
    // SECTORS EXT, 512-byte sectors, no DMA/AHCI/NVMe. The block request layer
    // issues normal requests through the IRQ14 completion boundary.
    void ata_pio_primary_master_init(AtaPioDevice *__device) noexcept;
    // Same bounded PIO backend for an independent persistent /rw test disk on a
    // helper-owned ISA IDE controller. It stays off the default BIOS/PIIX boot
    // channels so it does not affect drive 0x80 boot discovery.
    void ata_pio_persistent_test_init(AtaPioDevice *__device) noexcept;
    void ata_pio_primary_irq() noexcept;
}   // namespace block
NAMESPACE_DRIVER_END

#endif   // _DRIVERS_BLOCK_ATA_PIO_H
