#include <drivers/irqchip/ioapic.h>

#include <bigos/memory.h>

NAMESPACE_DRIVER_BEG
namespace irqchip::ioapic {
    namespace {
        constexpr uint32_t IOREGSEL = 0x00;
        constexpr uint32_t IOWIN = 0x10;
        constexpr uint8_t REG_ID = 0x00;
        constexpr uint8_t REG_VERSION = 0x01;
        constexpr uint8_t REG_REDIRECTION_BASE = 0x10;
        constexpr uint64_t REDIR_MASKED = 1ull << 16;
        constexpr uint64_t REDIR_POLARITY_LOW = 1ull << 13;
        constexpr uint64_t REDIR_TRIGGER_LEVEL = 1ull << 15;
        constexpr uint8_t MAX_ISA_IRQ = 15;
        constexpr bigos::mm::PageAttr MMIO_PAGE_ATTR =
            bigos::mm::page_attr::KERNEL_DEFAULT | bigos::mm::page_attr::NO_EXECUTE | (1ull << 3) | (1ull << 4);

        Status g_status = Status::Uninitialized;
        volatile uint32_t *g_mmio = nullptr;
        uint32_t g_phys = 0;

        uint32_t read(uint8_t __reg) noexcept {
            g_mmio[IOREGSEL / sizeof(uint32_t)] = __reg;
            return g_mmio[IOWIN / sizeof(uint32_t)];
        }

        void write(uint8_t __reg, uint32_t __value) noexcept {
            g_mmio[IOREGSEL / sizeof(uint32_t)] = __reg;
            g_mmio[IOWIN / sizeof(uint32_t)] = __value;
        }

        volatile uint32_t *map_mmio_page(uint32_t __phys) noexcept {
            if (g_mmio != nullptr)
                return g_mmio;
            if (bigos::mm::is_direct_mapped_phys(__phys, 4096))
                return (volatile uint32_t *)bigos::mm::phys_to_direct(__phys);
            void *vaddr = bigos::alloc_kernel_pages(1, 0);
            if (vaddr == nullptr)
                return nullptr;
            if (!bigos::mm::map_page((uint64_t)vaddr, __phys, MMIO_PAGE_ATTR))
                return nullptr;
            return (volatile uint32_t *)vaddr;
        }
    }   // namespace

    bool init(uint32_t __phys_address) noexcept {
        g_mmio = map_mmio_page(__phys_address);
        if (g_mmio == nullptr) {
            g_status = Status::DirectMapUnavailable;
            return false;
        }

        g_phys = __phys_address;
        g_status = Status::Ready;
        return true;
    }

    Status status() noexcept {
        return g_status;
    }

    uint32_t id() noexcept {
        if (g_mmio == nullptr)
            return 0xffffffffu;
        return (read(REG_ID) >> 24) & 0x0fu;
    }

    uint32_t version() noexcept {
        if (g_mmio == nullptr)
            return 0;
        return read(REG_VERSION);
    }

    bool route_irq(const RedirectionConfig &__config) noexcept {
        if (g_mmio == nullptr || __config.irq > MAX_ISA_IRQ || __config.vector < 0x20) {
            g_status = Status::InvalidInput;
            return false;
        }

        uint64_t entry = __config.vector;
        if (__config.masked)
            entry |= REDIR_MASKED;
        if (__config.polarity == Polarity::ActiveLow)
            entry |= REDIR_POLARITY_LOW;
        if (__config.trigger == TriggerMode::Level)
            entry |= REDIR_TRIGGER_LEVEL;
        entry |= (uint64_t)(__config.destination_apic_id & 0xffu) << 56;

        const uint8_t reg = REG_REDIRECTION_BASE + __config.irq * 2;
        write(reg, (uint32_t)entry);
        write(reg + 1, (uint32_t)(entry >> 32));
        g_status = Status::Ready;
        return true;
    }

    bool route_irq(uint8_t __irq, uint8_t __vector, uint32_t __destination_apic_id, bool __masked) noexcept {
        const RedirectionConfig config = {
            __irq,
            __vector,
            __destination_apic_id,
            __masked,
            TriggerMode::Edge,
            Polarity::ActiveHigh,
        };
        return route_irq(config);
    }
}   // namespace irqchip::ioapic
NAMESPACE_DRIVER_END
