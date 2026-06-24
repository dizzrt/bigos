#ifndef _BIG_DRIVERS_PCI_MSIX_H
#define _BIG_DRIVERS_PCI_MSIX_H

#include <bigos/memory.h>
#include <bigos/types.h>
#include <drivers/pci/config.h>
#include <irq/interrupt.h>

NAMESPACE_DRIVER_BEG
namespace pci::msix {
    constexpr uint8_t CAPABILITY_ID = 0x11;
    constexpr uint16_t MAX_TABLE_ENTRIES = 2048;

    enum class Status : uint8_t {
        Ok,
        InvalidArgument,
        UnsupportedContext,
        NoDevice,
        Unsupported,
        BadCapabilityList,
        UnsupportedBar,
        OutOfRange,
        MappingFailed,
        VectorAllocFailed,
        LapicUnavailable,
    };

    struct CapabilityInfo {
        FunctionAddress address;
        uint8_t capability_offset;
        uint16_t table_size;
        uint8_t table_bar_index;
        uint32_t table_offset;
        uint8_t pba_bar_index;
        uint32_t pba_offset;
        bool enabled;
        bool function_masked;
    };

    struct TableEntry {
        volatile uint32_t message_address_low;
        volatile uint32_t message_address_high;
        volatile uint32_t message_data;
        volatile uint32_t vector_control;
    } __attribute__((packed));

    static_assert(sizeof(TableEntry) == 16);

    struct Mapping {
        bigos::mm::DeviceMmioMapping table_mapping;
        bigos::mm::DeviceMmioMapping pba_mapping;
        volatile TableEntry *table;
        volatile uint64_t *pba;
        uint16_t table_size;
        uint64_t pba_bytes;
        bool valid;
    };

    struct ProgrammedVector {
        uint16_t entry_index;
        uint8_t vector;
        uint32_t target_apic_id;
    };

    bool context_allows_msix_config() noexcept;
    Status find_capability(FunctionAddress __address, CapabilityInfo *__out) noexcept;
    Status refresh_control(CapabilityInfo *__info) noexcept;
    Status set_function_mask(const CapabilityInfo &__info, bool __masked) noexcept;
    Status set_enable(const CapabilityInfo &__info, bool __enabled) noexcept;
    Status map_table_and_pba(const CapabilityInfo &__info, Mapping *__out) noexcept;
    Status set_entry_mask(Mapping &__mapping, uint16_t __entry_index, bool __masked) noexcept;
    Status program_entry(Mapping &__mapping, uint16_t __entry_index, uint8_t __vector, uint32_t __target_apic_id,
        bool __masked_after_programming) noexcept;
    Status allocate_and_program_entry(Mapping &__mapping, uint16_t __entry_index, bigos::irq::IRQHandler __handler,
        uint32_t __target_apic_id, ProgrammedVector *__out) noexcept;
    const char *status_name(Status __status) noexcept;

#ifdef BIGOS_PCI_MSIX_SMOKE
    bool smoke() noexcept;
#endif
}   // namespace pci::msix
NAMESPACE_DRIVER_END

#endif   // _BIG_DRIVERS_PCI_MSIX_H
