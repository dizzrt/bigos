#include <drivers/pci/msix.h>

#include <bigos/io.h>
#include <bigos/percpu.h>
#include <drivers/irqchip/lapic.h>

NAMESPACE_DRIVER_BEG
namespace pci::msix {
    namespace {
        constexpr uint8_t REG_MESSAGE_CONTROL = 0x02;
        constexpr uint8_t REG_TABLE = 0x04;
        constexpr uint8_t REG_PBA = 0x08;
        constexpr uint16_t MESSAGE_CONTROL_TABLE_SIZE_MASK = 0x07ffu;
        constexpr uint16_t MESSAGE_CONTROL_FUNCTION_MASK = 1u << 14;
        constexpr uint16_t MESSAGE_CONTROL_ENABLE = 1u << 15;
        constexpr uint32_t BIR_MASK = 0x7u;
        constexpr uint32_t OFFSET_MASK = 0xfffffff8u;
        constexpr uint32_t VECTOR_CONTROL_MASKED = 1u;
        constexpr uint64_t X86_MSI_ADDRESS_BASE = 0xfee00000ull;
        constexpr uint32_t X86_MSI_DESTINATION_SHIFT = 12;
        constexpr uint32_t X86_MSI_DESTINATION_MASK = 0xffu;

        Status from_pci_status(pci::Status __status) noexcept {
            switch (__status) {
                case pci::Status::Ok:
                    return Status::Ok;
                case pci::Status::InvalidArgument:
                    return Status::InvalidArgument;
                case pci::Status::UnsupportedContext:
                    return Status::UnsupportedContext;
                case pci::Status::NoDevice:
                    return Status::NoDevice;
                case pci::Status::BadCapabilityList:
                    return Status::BadCapabilityList;
                case pci::Status::UnsupportedBar:
                    return Status::UnsupportedBar;
                default:
                    return Status::Unsupported;
            }
        }

        Status read_message_control(const CapabilityInfo &__info, uint16_t *__control) noexcept {
            return from_pci_status(
                pci::read_config16(__info.address, (uint8_t)(__info.capability_offset + REG_MESSAGE_CONTROL), __control));
        }

        Status write_message_control(const CapabilityInfo &__info, uint16_t __control) noexcept {
            return from_pci_status(pci::write_config16(
                __info.address, (uint8_t)(__info.capability_offset + REG_MESSAGE_CONTROL), __control));
        }

        Status update_control_bit(const CapabilityInfo &__info, uint16_t __bit, bool __set) noexcept {
            uint16_t control = 0;
            Status status = read_message_control(__info, &control);
            if (status != Status::Ok)
                return status;
            if (__set)
                control |= __bit;
            else
                control &= (uint16_t)~__bit;
            return write_message_control(__info, control);
        }

        bool range_within_bar(const pci::BarInfo &__bar, uint32_t __offset, uint64_t __len) noexcept {
            if (__bar.kind != pci::BarKind::Memory32 && __bar.kind != pci::BarKind::Memory64)
                return false;
            if (__len == 0 || __bar.size == 0 || __offset >= __bar.size)
                return false;
            return __len <= __bar.size - __offset;
        }

        uint64_t pba_bytes_for(uint16_t __table_size) noexcept {
            return (((uint64_t)__table_size + 63ull) / 64ull) * sizeof(uint64_t);
        }

        void compiler_barrier() noexcept {
            asm volatile("" ::: "memory");
        }
    }   // namespace

    bool context_allows_msix_config() noexcept {
        const bigos::cpu::LocalState &local = bigos::cpu::current_state();
        return local.irq_nesting_depth == 0 && local.nonblocking_depth == 0 && local.preemption_disable_depth == 0;
    }

    Status find_capability(FunctionAddress __address, CapabilityInfo *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;

        Capability caps[pci::MAX_CAPABILITIES] = {};
        uint8_t count = 0;
        pci::Status pci_status = pci::read_capabilities(__address, caps, pci::MAX_CAPABILITIES, &count);
        if (pci_status != pci::Status::Ok)
            return from_pci_status(pci_status);

        for (uint8_t i = 0; i < count && i < pci::MAX_CAPABILITIES; i++) {
            if (caps[i].id != CAPABILITY_ID)
                continue;

            uint16_t control = 0;
            uint32_t table = 0;
            uint32_t pba = 0;
            Status status = from_pci_status(pci::read_config16(
                __address, (uint8_t)(caps[i].offset + REG_MESSAGE_CONTROL), &control));
            if (status == Status::Ok)
                status = from_pci_status(pci::read_config32(__address, (uint8_t)(caps[i].offset + REG_TABLE), &table));
            if (status == Status::Ok)
                status = from_pci_status(pci::read_config32(__address, (uint8_t)(caps[i].offset + REG_PBA), &pba));
            if (status != Status::Ok)
                return status;

            const uint16_t table_size = (uint16_t)((control & MESSAGE_CONTROL_TABLE_SIZE_MASK) + 1);
            if (table_size == 0 || table_size > MAX_TABLE_ENTRIES)
                return Status::OutOfRange;

            *__out = {
                __address,
                caps[i].offset,
                table_size,
                (uint8_t)(table & BIR_MASK),
                table & OFFSET_MASK,
                (uint8_t)(pba & BIR_MASK),
                pba & OFFSET_MASK,
                (control & MESSAGE_CONTROL_ENABLE) != 0,
                (control & MESSAGE_CONTROL_FUNCTION_MASK) != 0,
            };
            return Status::Ok;
        }

        return Status::Unsupported;
    }

    Status refresh_control(CapabilityInfo *__info) noexcept {
        if (__info == nullptr)
            return Status::InvalidArgument;
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;

        uint16_t control = 0;
        Status status = read_message_control(*__info, &control);
        if (status != Status::Ok)
            return status;
        __info->enabled = (control & MESSAGE_CONTROL_ENABLE) != 0;
        __info->function_masked = (control & MESSAGE_CONTROL_FUNCTION_MASK) != 0;
        return Status::Ok;
    }

    Status set_function_mask(const CapabilityInfo &__info, bool __masked) noexcept {
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;
        return update_control_bit(__info, MESSAGE_CONTROL_FUNCTION_MASK, __masked);
    }

    Status set_enable(const CapabilityInfo &__info, bool __enabled) noexcept {
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;
        return update_control_bit(__info, MESSAGE_CONTROL_ENABLE, __enabled);
    }

    Status map_table_and_pba(const CapabilityInfo &__info, Mapping *__out) noexcept {
        if (__out == nullptr)
            return Status::InvalidArgument;
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;

        *__out = {};
        pci::BarInfo table_bar = {};
        pci::Status pci_status = pci::read_bar(__info.address, __info.table_bar_index, &table_bar);
        if (pci_status != pci::Status::Ok)
            return from_pci_status(pci_status);

        pci::BarInfo pba_bar = {};
        pci_status = pci::read_bar(__info.address, __info.pba_bar_index, &pba_bar);
        if (pci_status != pci::Status::Ok)
            return from_pci_status(pci_status);

        const uint64_t table_bytes = (uint64_t)__info.table_size * sizeof(TableEntry);
        const uint64_t pba_bytes = pba_bytes_for(__info.table_size);
        if (!range_within_bar(table_bar, __info.table_offset, table_bytes) ||
            !range_within_bar(pba_bar, __info.pba_offset, pba_bytes))
            return Status::OutOfRange;

        const bigos::mm::DeviceMmioMapping table_mapping = bigos::mm::map_device_mmio(
            table_bar.base + __info.table_offset, table_bytes, bigos::mm::DeviceMmioCachePolicy::Uncached);
        if (!table_mapping.valid)
            return Status::MappingFailed;

        const bigos::mm::DeviceMmioMapping pba_mapping = bigos::mm::map_device_mmio(
            pba_bar.base + __info.pba_offset, pba_bytes, bigos::mm::DeviceMmioCachePolicy::Uncached);
        if (!pba_mapping.valid)
            return Status::MappingFailed;

        __out->table_mapping = table_mapping;
        __out->pba_mapping = pba_mapping;
        __out->table = (volatile TableEntry *)table_mapping.vaddr;
        __out->pba = (volatile uint64_t *)pba_mapping.vaddr;
        __out->table_size = __info.table_size;
        __out->pba_bytes = pba_bytes;
        __out->valid = true;
        return Status::Ok;
    }

    Status set_entry_mask(Mapping &__mapping, uint16_t __entry_index, bool __masked) noexcept {
        if (!__mapping.valid || __mapping.table == nullptr || __entry_index >= __mapping.table_size)
            return Status::InvalidArgument;

        volatile TableEntry &entry = __mapping.table[__entry_index];
        uint32_t control = entry.vector_control;
        if (__masked)
            control |= VECTOR_CONTROL_MASKED;
        else
            control &= ~VECTOR_CONTROL_MASKED;
        entry.vector_control = control;
        compiler_barrier();
        return Status::Ok;
    }

    Status program_entry(Mapping &__mapping, uint16_t __entry_index, uint8_t __vector, uint32_t __target_apic_id,
        bool __masked_after_programming) noexcept {
        if (!__mapping.valid || __mapping.table == nullptr || __entry_index >= __mapping.table_size || __vector < 0x20)
            return Status::InvalidArgument;
        if ((__target_apic_id & ~X86_MSI_DESTINATION_MASK) != 0)
            return Status::OutOfRange;

        Status status = set_entry_mask(__mapping, __entry_index, true);
        if (status != Status::Ok)
            return status;

        volatile TableEntry &entry = __mapping.table[__entry_index];
        const uint64_t message_address =
            X86_MSI_ADDRESS_BASE | ((uint64_t)__target_apic_id << X86_MSI_DESTINATION_SHIFT);
        entry.message_address_high = (uint32_t)(message_address >> 32);
        entry.message_address_low = (uint32_t)message_address;
        entry.message_data = __vector;
        compiler_barrier();

        if (!__masked_after_programming)
            return set_entry_mask(__mapping, __entry_index, false);
        return Status::Ok;
    }

    Status allocate_and_program_entry(Mapping &__mapping, uint16_t __entry_index, bigos::irq::IRQHandler __handler,
        uint32_t __target_apic_id, ProgrammedVector *__out) noexcept {
        if (__handler == nullptr || __out == nullptr)
            return Status::InvalidArgument;
        if (!context_allows_msix_config())
            return Status::UnsupportedContext;

        uint8_t vector = 0;
        const bigos::irq::VectorAllocStatus alloc_status = bigos::irq::allocate_lapic_vector(__handler, &vector);
        if (alloc_status != bigos::irq::VectorAllocStatus::Ok)
            return Status::VectorAllocFailed;

        const Status status = program_entry(__mapping, __entry_index, vector, __target_apic_id, true);
        if (status != Status::Ok) {
            (void)bigos::irq::release_lapic_vector(vector);
            return status;
        }

        *__out = {__entry_index, vector, __target_apic_id};
        return Status::Ok;
    }

    const char *status_name(Status __status) noexcept {
        switch (__status) {
            case Status::Ok:
                return "ok";
            case Status::InvalidArgument:
                return "invalid-argument";
            case Status::UnsupportedContext:
                return "unsupported-context";
            case Status::NoDevice:
                return "no-device";
            case Status::Unsupported:
                return "unsupported";
            case Status::BadCapabilityList:
                return "bad-capability-list";
            case Status::UnsupportedBar:
                return "unsupported-bar";
            case Status::OutOfRange:
                return "out-of-range";
            case Status::MappingFailed:
                return "mapping-failed";
            case Status::VectorAllocFailed:
                return "vector-alloc-failed";
            case Status::LapicUnavailable:
                return "lapic-unavailable";
            default:
                return "unknown";
        }
    }

#ifdef BIGOS_PCI_MSIX_SMOKE
    namespace {
        constexpr uint32_t DELIVERY_WAIT_ITERATIONS = 100000;

        volatile uint32_t g_smoke_handler_count = 0;
        volatile uint64_t g_smoke_last_vector = 0;

        void smoke_irq_handler(bigos::irq::InterruptFrame *__frame) noexcept {
            g_smoke_handler_count++;
            g_smoke_last_vector = __frame != nullptr ? __frame->vector : 0;
        }

        bool scan_for_msix_capability() noexcept {
            CapabilityInfo info = {};
            bool saw_device = false;
            for (uint8_t dev = 0; dev < 32; dev++) {
                for (uint8_t func = 0; func < 8; func++) {
                    const FunctionAddress address = {0, dev, func};
                    DeviceId id = {};
                    if (pci::probe_device(address, &id) != pci::Status::Ok)
                        continue;
                    saw_device = true;
                    const Status status = find_capability(address, &info);
                    if (status == Status::Ok) {
                        bigos::serial_puts("BIGOS_PCI_MSIX_CAPABILITY_PARSED\n");
                        return true;
                    }
                    if (status != Status::Unsupported)
                        return false;
                }
            }

            bigos::serial_puts(saw_device ? "BIGOS_PCI_MSIX_CAPABILITY_SKIPPED unsupported\n" :
                                            "BIGOS_PCI_MSIX_CAPABILITY_SKIPPED no-device\n");
            return true;
        }

        bool controlled_trigger(Mapping &__mapping, uint16_t __entry_index) noexcept {
            if (!__mapping.valid || __mapping.table == nullptr || __entry_index >= __mapping.table_size)
                return false;
            const volatile TableEntry &entry = __mapping.table[__entry_index];
            if ((entry.vector_control & VECTOR_CONTROL_MASKED) != 0)
                return false;
            const uint8_t vector = (uint8_t)(entry.message_data & 0xffu);
            return driver::irqchip::lapic::send_fixed_ipi(driver::irqchip::lapic::id(), vector);
        }

        bool wait_for_handler(uint32_t __expected_count, uint8_t __expected_vector) noexcept {
            for (uint32_t i = 0; i < DELIVERY_WAIT_ITERATIONS; i++) {
                if (g_smoke_handler_count >= __expected_count && g_smoke_last_vector == __expected_vector)
                    return true;
                asm volatile("pause" ::: "memory");
            }
            return false;
        }
    }   // namespace

    bool smoke() noexcept {
        if (!context_allows_msix_config()) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED context\n");
            return false;
        }

        CapabilityInfo absent = {};
        if (find_capability({0xff, 31, 7}, &absent) != Status::NoDevice) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED no-device\n");
            return false;
        }

        if (!scan_for_msix_capability()) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED capability-scan\n");
            return false;
        }

        if (driver::irqchip::lapic::status() != driver::irqchip::lapic::Status::Enabled) {
            bigos::serial_puts("BIGOS_PCI_MSIX_SMOKE_SKIPPED lapic\n");
            return true;
        }

        static volatile TableEntry synthetic_table[1] = {};
        Mapping mapping = {};
        mapping.table = synthetic_table;
        mapping.table_size = 1;
        mapping.valid = true;

        ProgrammedVector programmed = {};
        Status status = allocate_and_program_entry(mapping, 0, &smoke_irq_handler, driver::irqchip::lapic::id(), &programmed);
        if (status != Status::Ok) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED program status=");
            bigos::serial_puts(status_name(status));
            bigos::serial_puts("\n");
            return false;
        }

        g_smoke_handler_count = 0;
        g_smoke_last_vector = 0;
        if (controlled_trigger(mapping, 0) || g_smoke_handler_count != 0) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED mask\n");
            (void)bigos::irq::release_lapic_vector(programmed.vector);
            return false;
        }

        if (set_entry_mask(mapping, 0, false) != Status::Ok || !controlled_trigger(mapping, 0) ||
            !wait_for_handler(1, programmed.vector)) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED delivery\n");
            (void)bigos::irq::release_lapic_vector(programmed.vector);
            return false;
        }

        if (mapping.table[0].message_data != programmed.vector) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED vector-match\n");
            (void)bigos::irq::release_lapic_vector(programmed.vector);
            return false;
        }

        if (set_entry_mask(mapping, 0, true) != Status::Ok || controlled_trigger(mapping, 0) ||
            g_smoke_handler_count != 1) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED remask\n");
            (void)bigos::irq::release_lapic_vector(programmed.vector);
            return false;
        }

        if (bigos::irq::release_lapic_vector(programmed.vector) != bigos::irq::VectorAllocStatus::Ok) {
            bigos::serial_puts("BIGOS_PCI_MSIX_FAILED vector-release\n");
            return false;
        }

        bigos::serial_puts("BIGOS_PCI_MSIX_DELIVERY_PASSED\n");
        bigos::serial_puts("BIGOS_PCI_MSIX_PASSED\n");
        return true;
    }
#endif
}   // namespace pci::msix
NAMESPACE_DRIVER_END
