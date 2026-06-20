#include <bigos/percpu.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/panic.h>
#include <drivers/irqchip/lapic.h>

NAMESPACE_BIGOS_BEG
namespace cpu {
    namespace {
        constexpr uint32_t MP_FLOATING_SIGNATURE = 0x5f504d5fu;   // "_MP_"
        constexpr uint32_t MP_CONFIG_SIGNATURE = 0x504d4350u;     // "PCMP"
        constexpr uint32_t BIOS_ROM_BASE = 0x000f0000u;
        constexpr uint32_t BIOS_ROM_LENGTH = 0x00010000u;
        constexpr uint32_t EBDA_SEGMENT_ADDRESS = 0x0000040eu;
        constexpr uint32_t BASE_MEMORY_KB_ADDRESS = 0x00000413u;
        constexpr uint8_t MP_ENTRY_PROCESSOR = 0;
        constexpr uint8_t MP_ENTRY_IOAPIC = 2;
        constexpr uint8_t MP_CPU_ENABLED = 0x01;
        constexpr uint8_t MP_CPU_BSP = 0x02;
        constexpr uint8_t MP_IOAPIC_ENABLED = 0x01;
        constexpr uint32_t PAGE_SIZE = 4096;
        constexpr uint32_t ACPI_MAX_TABLE_BYTES = 65536;
        constexpr uint8_t ACPI_MADT_TYPE_LOCAL_APIC = 0;
        constexpr uint8_t ACPI_MADT_TYPE_IOAPIC = 1;
        constexpr uint32_t ACPI_LOCAL_APIC_ENABLED = 0x1;

        bool signature_equal(const char *__a, const char *__b, uint32_t __len) noexcept {
            for (uint32_t i = 0; i < __len; i++) {
                if (__a[i] != __b[i])
                    return false;
            }
            return true;
        }

        struct MpFloatingPointer {
            uint32_t signature;
            uint32_t config_table_phys;
            uint8_t length_16;
            uint8_t spec_rev;
            uint8_t checksum;
            uint8_t features[5];
        } __attribute__((packed));

        struct MpConfigTableHeader {
            uint32_t signature;
            uint16_t base_table_length;
            uint8_t spec_rev;
            uint8_t checksum;
            char oem_id[8];
            char product_id[12];
            uint32_t oem_table_phys;
            uint16_t oem_table_size;
            uint16_t entry_count;
            uint32_t lapic_address;
            uint16_t extended_table_length;
            uint8_t extended_table_checksum;
            uint8_t reserved;
        } __attribute__((packed));

        struct MpProcessorEntry {
            uint8_t type;
            uint8_t apic_id;
            uint8_t apic_version;
            uint8_t flags;
            uint32_t cpu_signature;
            uint32_t feature_flags;
            uint32_t reserved[2];
        } __attribute__((packed));

        struct MpIoApicEntry {
            uint8_t type;
            uint8_t id;
            uint8_t version;
            uint8_t flags;
            uint32_t address;
        } __attribute__((packed));

        struct RsdpDescriptor {
            char signature[8];
            uint8_t checksum;
            char oem_id[6];
            uint8_t revision;
            uint32_t rsdt_address;
            uint32_t length;
            uint64_t xsdt_address;
            uint8_t extended_checksum;
            uint8_t reserved[3];
        } __attribute__((packed));

        struct AcpiSdtHeader {
            char signature[4];
            uint32_t length;
            uint8_t revision;
            uint8_t checksum;
            char oem_id[6];
            char oem_table_id[8];
            uint32_t oem_revision;
            uint32_t creator_id;
            uint32_t creator_revision;
        } __attribute__((packed));

        struct AcpiMadtHeader {
            AcpiSdtHeader header;
            uint32_t local_apic_address;
            uint32_t flags;
        } __attribute__((packed));

        struct AcpiEntryHeader {
            uint8_t type;
            uint8_t length;
        } __attribute__((packed));

        struct AcpiMadtLocalApic {
            AcpiEntryHeader header;
            uint8_t processor_id;
            uint8_t apic_id;
            uint32_t flags;
        } __attribute__((packed));

        struct AcpiMadtIoApic {
            AcpiEntryHeader header;
            uint8_t ioapic_id;
            uint8_t reserved;
            uint32_t address;
            uint32_t global_system_interrupt_base;
        } __attribute__((packed));

        CpuSlot g_cpu_slots[MAX_CPUS];
        IoApicDescriptor g_ioapics[MAX_IOAPICS];
        TopologyState g_topology_state = TopologyState::BootstrapOnly;
        CpuId g_online_cpu_count = 0;
        CpuId g_discovered_cpu_count = 0;
        uint32_t g_ioapic_count = 0;
        bool g_initialized = false;
        bool g_topology_initialized = false;

        const uint8_t *phys_ptr(uint32_t __phys) noexcept {
            return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(__phys));
        }

        const void *map_physical(uint64_t __phys, uint32_t __len) noexcept {
            if (__len == 0)
                return nullptr;
            if (__phys + __len <= 0x100000ull)
                return reinterpret_cast<const void *>(static_cast<uintptr_t>(__phys));
            if (bigos::mm::is_direct_mapped_phys(__phys, __len))
                return bigos::mm::phys_to_direct(__phys);

            const uint64_t page_base = __phys & ~(uint64_t)(PAGE_SIZE - 1);
            const uint64_t page_offset = __phys - page_base;
            const uint32_t pages = (uint32_t)((page_offset + __len + PAGE_SIZE - 1) / PAGE_SIZE);
            void *vaddr = bigos::alloc_kernel_pages(pages, 0);
            if (vaddr == nullptr)
                return nullptr;

            for (uint32_t i = 0; i < pages; i++) {
                if (!bigos::mm::map_page((uint64_t)vaddr + i * PAGE_SIZE, page_base + i * PAGE_SIZE,
                        bigos::mm::page_attr::KERNEL_DEFAULT | bigos::mm::page_attr::NO_EXECUTE))
                    return nullptr;
            }
            return reinterpret_cast<const uint8_t *>(vaddr) + page_offset;
        }

        uint8_t checksum(const void *__ptr, uint32_t __len) noexcept {
            const uint8_t *bytes = static_cast<const uint8_t *>(__ptr);
            uint8_t sum = 0;
            for (uint32_t i = 0; i < __len; i++)
                sum = static_cast<uint8_t>(sum + bytes[i]);
            return sum;
        }

        bool valid_checksum(const void *__ptr, uint32_t __len) noexcept {
            return checksum(__ptr, __len) == 0;
        }

        uint32_t read_bsp_apic_id() noexcept {
            uint32_t eax = 1;
            uint32_t ebx = 0;
            uint32_t ecx = 0;
            uint32_t edx = 0;
            asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
            return (ebx >> 24) & 0xffu;
        }

        void reset_slot(CpuId __id) noexcept {
            g_cpu_slots[__id] = {
                __id,
                INVALID_APIC_ID,
                __id == BOOTSTRAP_CPU_ID ? CpuRole::Bootstrap : CpuRole::Application,
                CpuStartupState::Empty,
                CpuFailureReason::None,
                LocalTimerState::Disabled,
                0,
                {
                    __id,
                    nullptr,
                    nullptr,
                    0,
                    0,
                    0,
                    0,
                    false,
                },
            };
        }

        void reset_ioapics() noexcept {
            for (uint32_t i = 0; i < MAX_IOAPICS; i++)
                g_ioapics[i] = {0, 0, false, 0};
            g_ioapic_count = 0;
        }

        [[noreturn]] void unsupported_cpu_access(CpuId __id) noexcept {
            bigos::kpanic(bigos::PanicCode::Generic, "cpu-local", "unsupported cpu id %u\n", __id);
        }

        CpuSlot &mutable_slot_for(CpuId __id) noexcept {
            if (__id >= MAX_CPUS || g_cpu_slots[__id].startup_state == CpuStartupState::Empty)
                unsupported_cpu_access(__id);
            return g_cpu_slots[__id];
        }

        [[noreturn]] void unsupported_ioapic_access(uint32_t __index) noexcept {
            bigos::kpanic(bigos::PanicCode::Generic, "cpu-local", "unsupported ioapic index %u\n", __index);
        }

        const MpFloatingPointer *scan_mp_floating(uint32_t __base, uint32_t __length) noexcept {
            const uint32_t aligned_base = (__base + 15u) & ~15u;
            const uint32_t end = __base + __length;
            for (uint32_t phys = aligned_base; phys + sizeof(MpFloatingPointer) <= end; phys += 16u) {
                const MpFloatingPointer *mp = reinterpret_cast<const MpFloatingPointer *>(phys_ptr(phys));
                if (mp->signature == MP_FLOATING_SIGNATURE && mp->length_16 != 0 &&
                    valid_checksum(mp, static_cast<uint32_t>(mp->length_16) * 16u))
                    return mp;
            }
            return nullptr;
        }

        const MpFloatingPointer *find_mp_floating() noexcept {
            const uint16_t ebda_segment = *reinterpret_cast<const uint16_t *>(phys_ptr(EBDA_SEGMENT_ADDRESS));
            if (ebda_segment != 0) {
                const MpFloatingPointer *mp = scan_mp_floating(static_cast<uint32_t>(ebda_segment) << 4, 1024);
                if (mp != nullptr)
                    return mp;
            }

            const uint16_t base_memory_kb = *reinterpret_cast<const uint16_t *>(phys_ptr(BASE_MEMORY_KB_ADDRESS));
            if (base_memory_kb > 1) {
                const uint32_t top_kb = static_cast<uint32_t>(base_memory_kb) * 1024u;
                const MpFloatingPointer *mp = scan_mp_floating(top_kb - 1024u, 1024);
                if (mp != nullptr)
                    return mp;
            }

            const MpFloatingPointer *mp = scan_mp_floating(BIOS_ROM_BASE, BIOS_ROM_LENGTH);
            if (mp != nullptr)
                return mp;

            return scan_mp_floating(0x00070000u, 0x00090000u);
        }

        bool apic_id_already_recorded(uint8_t __apic_id) noexcept {
            for (CpuId id = 0; id < MAX_CPUS; id++) {
                if (g_cpu_slots[id].startup_state != CpuStartupState::Empty &&
                    g_cpu_slots[id].apic_id == static_cast<uint32_t>(__apic_id))
                    return true;
            }
            return false;
        }

        CpuId next_empty_cpu_slot() noexcept {
            for (CpuId id = 0; id < MAX_CPUS; id++) {
                if (g_cpu_slots[id].startup_state == CpuStartupState::Empty)
                    return id;
            }
            return MAX_CPUS;
        }

        void record_processor(const MpProcessorEntry *__entry) noexcept {
            if ((__entry->flags & MP_CPU_ENABLED) == 0)
                return;

            const bool is_bsp = (__entry->flags & MP_CPU_BSP) != 0;
            if (is_bsp) {
                CpuSlot &bsp = g_cpu_slots[BOOTSTRAP_CPU_ID];
                bsp.apic_id = __entry->apic_id;
                bsp.role = CpuRole::Bootstrap;
                bsp.startup_state = CpuStartupState::Online;
                bsp.failure = CpuFailureReason::None;
                return;
            }

            if (apic_id_already_recorded(__entry->apic_id)) {
                g_topology_state = TopologyState::MpTableInvalid;
                return;
            }

            const CpuId id = next_empty_cpu_slot();
            if (id >= MAX_CPUS) {
                g_topology_state = TopologyState::CapacityExceeded;
                return;
            }

            CpuSlot &slot = g_cpu_slots[id];
            slot.apic_id = __entry->apic_id;
            slot.role = CpuRole::Application;
            slot.startup_state = CpuStartupState::Offline;
            slot.failure = CpuFailureReason::None;
            slot.timer_state = LocalTimerState::Disabled;
            g_discovered_cpu_count++;
        }

        void record_ioapic(const MpIoApicEntry *__entry) noexcept {
            if (g_ioapic_count >= MAX_IOAPICS) {
                g_topology_state = TopologyState::CapacityExceeded;
                return;
            }

            g_ioapics[g_ioapic_count++] = {
                __entry->id,
                __entry->version,
                (__entry->flags & MP_IOAPIC_ENABLED) != 0,
                __entry->address,
            };
        }

        void record_madt_local_apic(const AcpiMadtLocalApic *__entry) noexcept {
            if ((__entry->flags & ACPI_LOCAL_APIC_ENABLED) == 0)
                return;

            const uint8_t apic_id = __entry->apic_id;
            const bool is_bsp = apic_id == read_bsp_apic_id();
            if (is_bsp) {
                CpuSlot &bsp = g_cpu_slots[BOOTSTRAP_CPU_ID];
                bsp.apic_id = apic_id;
                bsp.role = CpuRole::Bootstrap;
                bsp.startup_state = CpuStartupState::Online;
                bsp.failure = CpuFailureReason::None;
                return;
            }

            if (apic_id_already_recorded(apic_id))
                return;

            const CpuId id = next_empty_cpu_slot();
            if (id >= MAX_CPUS) {
                g_topology_state = TopologyState::CapacityExceeded;
                return;
            }

            CpuSlot &slot = g_cpu_slots[id];
            slot.apic_id = apic_id;
            slot.role = CpuRole::Application;
            slot.startup_state = CpuStartupState::Offline;
            slot.failure = CpuFailureReason::None;
            slot.timer_state = LocalTimerState::Disabled;
            g_discovered_cpu_count++;
        }

        void record_madt_ioapic(const AcpiMadtIoApic *__entry) noexcept {
            if (g_ioapic_count >= MAX_IOAPICS) {
                g_topology_state = TopologyState::CapacityExceeded;
                return;
            }

            g_ioapics[g_ioapic_count++] = {
                __entry->ioapic_id,
                0,
                true,
                __entry->address,
            };
        }

        uint32_t mp_entry_size(uint8_t __type) noexcept {
            switch (__type) {
                case MP_ENTRY_PROCESSOR:
                    return sizeof(MpProcessorEntry);
                case MP_ENTRY_IOAPIC:
                    return sizeof(MpIoApicEntry);
                case 1:
                case 3:
                case 4:
                    return 8;
                default:
                    return 0;
            }
        }

        CpuId cpu_id_for_apic_id(uint32_t __apic_id) noexcept {
            for (CpuId id = 0; id < MAX_CPUS; id++) {
                if (g_cpu_slots[id].startup_state != CpuStartupState::Empty && g_cpu_slots[id].apic_id == __apic_id)
                    return id;
            }
            return BOOTSTRAP_CPU_ID;
        }

        const RsdpDescriptor *scan_rsdp(uint32_t __base, uint32_t __length) noexcept {
            const uint32_t aligned_base = (__base + 15u) & ~15u;
            const uint32_t end = __base + __length;
            for (uint32_t phys = aligned_base; phys + 20 <= end; phys += 16u) {
                const RsdpDescriptor *rsdp = reinterpret_cast<const RsdpDescriptor *>(phys_ptr(phys));
                if (!signature_equal(rsdp->signature, "RSD PTR ", 8))
                    continue;
                if (!valid_checksum(rsdp, 20))
                    continue;
                if (rsdp->revision >= 2 && rsdp->length >= sizeof(RsdpDescriptor) &&
                    !valid_checksum(rsdp, rsdp->length))
                    continue;
                return rsdp;
            }
            return nullptr;
        }

        const RsdpDescriptor *find_rsdp() noexcept {
            const uint16_t ebda_segment = *reinterpret_cast<const uint16_t *>(phys_ptr(EBDA_SEGMENT_ADDRESS));
            if (ebda_segment != 0) {
                const RsdpDescriptor *rsdp = scan_rsdp(static_cast<uint32_t>(ebda_segment) << 4, 1024);
                if (rsdp != nullptr)
                    return rsdp;
            }

            return scan_rsdp(BIOS_ROM_BASE, BIOS_ROM_LENGTH);
        }

        const AcpiSdtHeader *map_sdt(uint64_t __phys) noexcept {
            const AcpiSdtHeader *header = static_cast<const AcpiSdtHeader *>(map_physical(__phys, sizeof(AcpiSdtHeader)));
            if (header == nullptr || header->length < sizeof(AcpiSdtHeader) ||
                header->length > ACPI_MAX_TABLE_BYTES)
                return nullptr;

            const AcpiSdtHeader *full = static_cast<const AcpiSdtHeader *>(map_physical(__phys, header->length));
            if (full == nullptr || !valid_checksum(full, full->length))
                return nullptr;
            return full;
        }

        const AcpiMadtHeader *find_madt_from_root(const AcpiSdtHeader *__root, bool __xsdt) noexcept {
            if (__root == nullptr)
                return nullptr;
            if (__xsdt && !signature_equal(__root->signature, "XSDT", 4))
                return nullptr;
            if (!__xsdt && !signature_equal(__root->signature, "RSDT", 4))
                return nullptr;

            const uint32_t entry_size = __xsdt ? 8 : 4;
            if (__root->length < sizeof(AcpiSdtHeader) || (__root->length - sizeof(AcpiSdtHeader)) % entry_size != 0)
                return nullptr;

            const uint8_t *entries = reinterpret_cast<const uint8_t *>(__root) + sizeof(AcpiSdtHeader);
            const uint32_t count = (__root->length - sizeof(AcpiSdtHeader)) / entry_size;
            for (uint32_t i = 0; i < count; i++) {
                uint64_t phys = 0;
                if (__xsdt)
                    phys = reinterpret_cast<const uint64_t *>(entries)[i];
                else
                    phys = reinterpret_cast<const uint32_t *>(entries)[i];
                const AcpiSdtHeader *table = map_sdt(phys);
                if (table != nullptr && signature_equal(table->signature, "APIC", 4))
                    return reinterpret_cast<const AcpiMadtHeader *>(table);
            }
            return nullptr;
        }

        bool parse_acpi_madt() noexcept {
            bigos::serial_puts("BIGOS_ACPI_MADT_TRY\n");
            const RsdpDescriptor *rsdp = find_rsdp();
            if (rsdp == nullptr) {
                bigos::serial_puts("BIGOS_ACPI_RSDP_MISSING\n");
                g_topology_state = TopologyState::AcpiMadtMissing;
                return false;
            }
            bigos::serial_puts("BIGOS_ACPI_RSDP_FOUND\n");

            const AcpiMadtHeader *madt = nullptr;
            if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
                bigos::serial_puts("BIGOS_ACPI_XSDT_TRY\n");
                madt = find_madt_from_root(map_sdt(rsdp->xsdt_address), true);
            }
            if (madt == nullptr && rsdp->rsdt_address != 0) {
                bigos::serial_puts("BIGOS_ACPI_RSDT_TRY\n");
                madt = find_madt_from_root(map_sdt(rsdp->rsdt_address), false);
            }
            if (madt == nullptr || madt->header.length < sizeof(AcpiMadtHeader)) {
                bigos::serial_puts("BIGOS_ACPI_MADT_INVALID\n");
                g_topology_state = TopologyState::AcpiMadtInvalid;
                return false;
            }
            bigos::serial_puts("BIGOS_ACPI_MADT_LOADED\n");

            g_topology_state = TopologyState::AcpiMadtLoaded;
            const uint8_t *entry = reinterpret_cast<const uint8_t *>(madt) + sizeof(AcpiMadtHeader);
            const uint8_t *end = reinterpret_cast<const uint8_t *>(madt) + madt->header.length;
            while (entry + sizeof(AcpiEntryHeader) <= end) {
                const AcpiEntryHeader *header = reinterpret_cast<const AcpiEntryHeader *>(entry);
                if (header->length < sizeof(AcpiEntryHeader) || entry + header->length > end) {
                    bigos::serial_puts("BIGOS_ACPI_MADT_ENTRY_INVALID\n");
                    g_topology_state = TopologyState::AcpiMadtInvalid;
                    return false;
                }

                if (header->type == ACPI_MADT_TYPE_LOCAL_APIC && header->length >= sizeof(AcpiMadtLocalApic))
                    record_madt_local_apic(reinterpret_cast<const AcpiMadtLocalApic *>(entry));
                else if (header->type == ACPI_MADT_TYPE_IOAPIC && header->length >= sizeof(AcpiMadtIoApic))
                    record_madt_ioapic(reinterpret_cast<const AcpiMadtIoApic *>(entry));
                entry += header->length;
            }

            bigos::serial_puts("BIGOS_ACPI_MADT_DONE\n");
            return g_discovered_cpu_count > 1;
        }
    }   // namespace

    void init_bootstrap_cpu() noexcept {
        if (g_initialized)
            return;

        for (CpuId id = 0; id < MAX_CPUS; id++)
            reset_slot(id);

        CpuSlot &bsp = g_cpu_slots[BOOTSTRAP_CPU_ID];
        bsp.apic_id = read_bsp_apic_id();
        bsp.role = CpuRole::Bootstrap;
        bsp.startup_state = CpuStartupState::Online;
        bsp.failure = CpuFailureReason::None;
        bsp.timer_state = LocalTimerState::Disabled;
        g_online_cpu_count = 1;
        g_discovered_cpu_count = 1;
        reset_ioapics();
        g_topology_state = TopologyState::BootstrapOnly;
        g_initialized = true;
    }

    void init_topology_from_mp() noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (g_topology_initialized)
            return;

        g_topology_initialized = true;
        const MpFloatingPointer *mp = find_mp_floating();
        if (mp == nullptr || mp->config_table_phys == 0) {
            g_topology_state = TopologyState::MpTableMissing;
            (void)parse_acpi_madt();
            return;
        }

        const MpConfigTableHeader *config =
            reinterpret_cast<const MpConfigTableHeader *>(phys_ptr(mp->config_table_phys));
        if (config->signature != MP_CONFIG_SIGNATURE || config->base_table_length < sizeof(MpConfigTableHeader) ||
            !valid_checksum(config, config->base_table_length)) {
            g_topology_state = TopologyState::MpTableInvalid;
            (void)parse_acpi_madt();
            return;
        }

        g_topology_state = TopologyState::MpTableLoaded;
        const uint8_t *entry = reinterpret_cast<const uint8_t *>(config) + sizeof(MpConfigTableHeader);
        const uint8_t *end = reinterpret_cast<const uint8_t *>(config) + config->base_table_length;
        for (uint16_t i = 0; i < config->entry_count && entry < end; i++) {
            const uint8_t type = *entry;
            const uint32_t size = mp_entry_size(type);
            if (size == 0 || entry + size > end) {
                g_topology_state = TopologyState::MpTableInvalid;
                return;
            }

            if (type == MP_ENTRY_PROCESSOR)
                record_processor(reinterpret_cast<const MpProcessorEntry *>(entry));
            else if (type == MP_ENTRY_IOAPIC)
                record_ioapic(reinterpret_cast<const MpIoApicEntry *>(entry));

            entry += size;
        }

        if (g_discovered_cpu_count <= 1)
            (void)parse_acpi_madt();
    }

    CpuId cpu_capacity() noexcept {
        return MAX_CPUS;
    }

    CpuId online_cpu_count() noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return g_online_cpu_count;
    }

    CpuId discovered_cpu_count() noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return g_discovered_cpu_count;
    }

    TopologyState topology_state() noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return g_topology_state;
    }

    uint32_t ioapic_count() noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return g_ioapic_count;
    }

    const CpuSlot &slot_for(CpuId __id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return mutable_slot_for(__id);
    }

    const IoApicDescriptor &ioapic_for(uint32_t __index) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (__index >= g_ioapic_count)
            unsupported_ioapic_access(__index);
        return g_ioapics[__index];
    }

    CpuId current_cpu_id() noexcept {
        if (g_initialized && driver::irqchip::lapic::status() == driver::irqchip::lapic::Status::Enabled)
            return cpu_id_for_apic_id(driver::irqchip::lapic::id());
        return BOOTSTRAP_CPU_ID;
    }

    bool is_bootstrap_cpu() noexcept {
        return current_cpu_id() == BOOTSTRAP_CPU_ID;
    }

    bool cpu_id_supported(CpuId __id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return __id < MAX_CPUS && g_cpu_slots[__id].startup_state != CpuStartupState::Empty;
    }

    bool cpu_online(CpuId __id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return __id < MAX_CPUS && g_cpu_slots[__id].startup_state == CpuStartupState::Online;
    }

    bool mark_cpu_online(CpuId __id, uint32_t __apic_id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (__id >= MAX_CPUS || g_cpu_slots[__id].startup_state == CpuStartupState::Empty)
            return false;

        CpuSlot &slot = g_cpu_slots[__id];
        if (slot.apic_id != __apic_id)
            return false;
        if (slot.startup_state != CpuStartupState::Online)
            g_online_cpu_count++;
        slot.startup_state = CpuStartupState::Online;
        slot.failure = CpuFailureReason::None;
        return true;
    }

    bool mark_cpu_failed(CpuId __id, CpuFailureReason __reason) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (__id >= MAX_CPUS || g_cpu_slots[__id].startup_state == CpuStartupState::Empty)
            return false;

        CpuSlot &slot = g_cpu_slots[__id];
        slot.startup_state = CpuStartupState::Failed;
        slot.failure = __reason;
        slot.timer_state = LocalTimerState::Failed;
        return true;
    }

    bool set_local_timer_state(CpuId __id, LocalTimerState __state) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (__id >= MAX_CPUS || g_cpu_slots[__id].startup_state == CpuStartupState::Empty)
            return false;

        g_cpu_slots[__id].timer_state = __state;
        return true;
    }

    bool record_local_timer_tick(CpuId __id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        if (__id >= MAX_CPUS || g_cpu_slots[__id].startup_state == CpuStartupState::Empty)
            return false;
        if (g_cpu_slots[__id].timer_state != LocalTimerState::Ready)
            return false;

        g_cpu_slots[__id].timer_ticks++;
        return true;
    }

    LocalState &state_for(CpuId __id) noexcept {
        if (!g_initialized)
            init_bootstrap_cpu();
        return mutable_slot_for(__id).local;
    }

    LocalState &current_state() noexcept {
        return state_for(current_cpu_id());
    }

    void set_current_thread(void *__t) noexcept {
        current_state().current_thread = __t;
    }

    void set_current_process(void *__process) noexcept {
        current_state().current_process = __process;
    }

    void set_current_address_space_root(uint64_t __root_phys) noexcept {
        current_state().current_address_space_root = __root_phys;
    }

    void set_irq_nesting_depth(uint32_t __depth) noexcept {
        current_state().irq_nesting_depth = __depth;
    }

    void set_nonblocking_depth(uint32_t __depth) noexcept {
        current_state().nonblocking_depth = __depth;
    }

    void set_preemption_disable_depth(uint32_t __depth) noexcept {
        current_state().preemption_disable_depth = __depth;
    }

    void set_reschedule_pending(bool __pending) noexcept {
        current_state().reschedule_pending = __pending;
    }
}   // namespace cpu
NAMESPACE_BIGOS_END
