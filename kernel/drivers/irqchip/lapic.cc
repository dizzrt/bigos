#include <drivers/irqchip/lapic.h>

#include <bigos/io.h>
#include <bigos/memory.h>
#include <drivers/timer/pit.h>

NAMESPACE_DRIVER_BEG
namespace irqchip::lapic {
    namespace {
        constexpr uint32_t CPUID_FEATURE_APIC = 1u << 9;
        constexpr uint32_t CPUID_FEATURE_X2APIC = 1u << 21;
        constexpr uint32_t IA32_APIC_BASE_MSR = 0x1bu;
        constexpr uint64_t APIC_BASE_X2APIC = 1ull << 10;
        constexpr uint64_t APIC_BASE_ENABLE = 1ull << 11;
        constexpr uint64_t APIC_BASE_MASK = 0x000ffffffffff000ull;
        constexpr uint32_t X2APIC_MSR_ID = 0x802;
        constexpr uint32_t X2APIC_MSR_EOI = 0x80b;
        constexpr uint32_t X2APIC_MSR_SVR = 0x80f;
        constexpr uint32_t X2APIC_MSR_ICR = 0x830;
        constexpr uint32_t X2APIC_MSR_LVT_TIMER = 0x832;
        constexpr uint32_t X2APIC_MSR_TIMER_INITIAL_COUNT = 0x838;
        constexpr uint32_t X2APIC_MSR_TIMER_CURRENT_COUNT = 0x839;
        constexpr uint32_t X2APIC_MSR_TIMER_DIVIDE = 0x83e;

        constexpr uint32_t REG_ID = 0x020;
        constexpr uint32_t REG_EOI = 0x0b0;
        constexpr uint32_t REG_SVR = 0x0f0;
        constexpr uint32_t REG_LVT_TIMER = 0x320;
        constexpr uint32_t REG_ICR_LOW = 0x300;
        constexpr uint32_t REG_ICR_HIGH = 0x310;
        constexpr uint32_t REG_TIMER_INITIAL_COUNT = 0x380;
        constexpr uint32_t REG_TIMER_CURRENT_COUNT = 0x390;
        constexpr uint32_t REG_TIMER_DIVIDE = 0x3e0;

        constexpr uint32_t SVR_ENABLE = 1u << 8;
        constexpr uint32_t SPURIOUS_VECTOR = 0xffu;
        constexpr uint32_t ICR_DELIVERY_STATUS = 1u << 12;
        constexpr uint32_t ICR_LEVEL_ASSERT = 1u << 14;
        constexpr uint32_t ICR_TRIGGER_LEVEL = 1u << 15;
        constexpr uint32_t ICR_DELIVERY_INIT = 5u << 8;
        constexpr uint32_t ICR_DELIVERY_SIPI = 6u << 8;
        constexpr uint32_t LVT_TIMER_MASKED = 1u << 16;
        constexpr uint32_t LVT_TIMER_PERIODIC = 1u << 17;
        constexpr uint32_t TIMER_DIVIDE_BY_16 = 0x3u;
        constexpr uint32_t TIMER_CALIBRATION_COUNT = 0xffffffffu;
        constexpr uint32_t PIT_CALIBRATION_TICKS = driver::timer::pit::CHANNEL0_DIVISOR / 2;
        constexpr uint32_t PIT_CALIBRATION_TIMEOUT = 2000000u;
        constexpr uint32_t SEND_TIMEOUT_ITERATIONS = 100000u;
        constexpr bigos::mm::PageAttr MMIO_PAGE_ATTR =
            bigos::mm::page_attr::KERNEL_DEFAULT | bigos::mm::page_attr::NO_EXECUTE | (1ull << 3) | (1ull << 4);

        Status g_status = Status::Uninitialized;
        uint64_t g_base_phys = 0;
        uint32_t g_timer_initial_count = 0;
        bool g_x2apic = false;
        volatile uint32_t *g_mmio = nullptr;

        void cpuid(uint32_t __leaf, uint32_t *__eax, uint32_t *__ebx, uint32_t *__ecx, uint32_t *__edx) noexcept {
            uint32_t eax = __leaf;
            uint32_t ebx = 0;
            uint32_t ecx = 0;
            uint32_t edx = 0;
            asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
            if (__eax != nullptr)
                *__eax = eax;
            if (__ebx != nullptr)
                *__ebx = ebx;
            if (__ecx != nullptr)
                *__ecx = ecx;
            if (__edx != nullptr)
                *__edx = edx;
        }

        uint64_t rdmsr(uint32_t __msr) noexcept {
            uint32_t lo = 0;
            uint32_t hi = 0;
            asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(__msr));
            return ((uint64_t)hi << 32) | lo;
        }

        void wrmsr(uint32_t __msr, uint64_t __value) noexcept {
            asm volatile("wrmsr" : : "c"(__msr), "a"((uint32_t)__value), "d"((uint32_t)(__value >> 32)) : "memory");
        }

        uint32_t read(uint32_t __offset) noexcept {
            if (g_x2apic) {
                switch (__offset) {
                    case REG_ID:
                        return (uint32_t)rdmsr(X2APIC_MSR_ID);
                    case REG_TIMER_CURRENT_COUNT:
                        return (uint32_t)rdmsr(X2APIC_MSR_TIMER_CURRENT_COUNT);
                    case REG_ICR_LOW:
                        return 0;
                    default:
                        return 0;
                }
            }
            return g_mmio[__offset / sizeof(uint32_t)];
        }

        void write(uint32_t __offset, uint32_t __value) noexcept {
            if (g_x2apic) {
                switch (__offset) {
                    case REG_EOI:
                        wrmsr(X2APIC_MSR_EOI, 0);
                        return;
                    case REG_SVR:
                        wrmsr(X2APIC_MSR_SVR, __value);
                        return;
                    case REG_LVT_TIMER:
                        wrmsr(X2APIC_MSR_LVT_TIMER, __value);
                        return;
                    case REG_TIMER_INITIAL_COUNT:
                        wrmsr(X2APIC_MSR_TIMER_INITIAL_COUNT, __value);
                        return;
                    case REG_TIMER_DIVIDE:
                        wrmsr(X2APIC_MSR_TIMER_DIVIDE, __value);
                        return;
                    default:
                        return;
                }
            }
            g_mmio[__offset / sizeof(uint32_t)] = __value;
        }

        bool wait_icr_idle() noexcept {
            for (uint32_t i = 0; i < SEND_TIMEOUT_ITERATIONS; i++) {
                if ((read(REG_ICR_LOW) & ICR_DELIVERY_STATUS) == 0)
                    return true;
                asm volatile("pause" : : : "memory");
            }
            g_status = Status::SendTimeout;
            return false;
        }

        bool send_ipi(uint32_t __apic_id, uint32_t __icr_low) noexcept {
            if (g_x2apic) {
                wrmsr(X2APIC_MSR_ICR, ((uint64_t)__apic_id << 32) | __icr_low);
                return true;
            }
            if (g_mmio == nullptr || !wait_icr_idle())
                return false;

            write(REG_ICR_HIGH, __apic_id << 24);
            write(REG_ICR_LOW, __icr_low);
            return wait_icr_idle();
        }

        volatile uint32_t *map_mmio_page(uint64_t __phys) noexcept {
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

        bool x2apic_supported() noexcept {
            uint32_t ecx = 0;
            cpuid(1, nullptr, nullptr, &ecx, nullptr);
            return (ecx & CPUID_FEATURE_X2APIC) != 0;
        }

        bool register_access_ready() noexcept {
            return g_x2apic || g_mmio != nullptr;
        }
    }   // namespace

    bool supported() noexcept {
        uint32_t edx = 0;
        cpuid(1, nullptr, nullptr, nullptr, &edx);
        return (edx & CPUID_FEATURE_APIC) != 0;
    }

    bool init() noexcept {
        bigos::serial_puts("BIGOS_LAPIC_INIT_BEGIN\n");
        uint32_t edx = 0;
        cpuid(1, nullptr, nullptr, nullptr, &edx);
        if ((edx & CPUID_FEATURE_APIC) == 0) {
            g_status = Status::Unavailable;
            return false;
        }
        bigos::serial_puts("BIGOS_LAPIC_CPUID_OK\n");

        uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
        g_base_phys = apic_base & APIC_BASE_MASK;
        bigos::serial_puts("BIGOS_LAPIC_MSR_READ\n");

        if ((apic_base & APIC_BASE_X2APIC) != 0 || g_x2apic) {
            if (!enable_x2apic()) {
                g_status = Status::Unavailable;
                return false;
            }
            bigos::serial_puts("BIGOS_LAPIC_X2APIC_READY\n");
            return true;
        }

        g_mmio = map_mmio_page(g_base_phys);
        if (g_mmio == nullptr) {
            g_status = Status::DirectMapUnavailable;
            return false;
        }
        bigos::serial_puts("BIGOS_LAPIC_MMIO_READY\n");

        wrmsr(IA32_APIC_BASE_MSR, apic_base | APIC_BASE_ENABLE);
        bigos::serial_puts("BIGOS_LAPIC_MSR_WRITTEN\n");
        write(REG_SVR, SVR_ENABLE | SPURIOUS_VECTOR);
        bigos::serial_puts("BIGOS_LAPIC_SVR_WRITTEN\n");
        g_status = Status::Enabled;
        return true;
    }

    bool enable_x2apic() noexcept {
        if (!x2apic_supported())
            return false;
        const uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
        g_base_phys = apic_base & APIC_BASE_MASK;
        g_x2apic = true;
        wrmsr(IA32_APIC_BASE_MSR, apic_base | APIC_BASE_ENABLE | APIC_BASE_X2APIC);
        write(REG_SVR, SVR_ENABLE | SPURIOUS_VECTOR);
        g_status = Status::Enabled;
        return true;
    }

    Status status() noexcept {
        return g_status;
    }

    uint32_t id() noexcept {
        if (g_x2apic)
            return read(REG_ID);
        if (g_mmio == nullptr)
            return 0xffffffffu;
        return read(REG_ID) >> 24;
    }

    uint64_t base_phys() noexcept {
        return g_base_phys;
    }

    void send_eoi() noexcept {
        if (g_x2apic || g_mmio != nullptr)
            write(REG_EOI, 0);
    }

    bool send_fixed_ipi(uint32_t __apic_id, uint8_t __vector) noexcept {
        return send_ipi(__apic_id, __vector);
    }

    bool send_init(uint32_t __apic_id) noexcept {
        return send_ipi(__apic_id, ICR_DELIVERY_INIT | ICR_LEVEL_ASSERT | ICR_TRIGGER_LEVEL);
    }

    bool send_sipi(uint32_t __apic_id, uint8_t __vector) noexcept {
        return send_ipi(__apic_id, ICR_DELIVERY_SIPI | __vector);
    }

    bool calibrate_timer_with_pit(uint32_t *__initial_count) noexcept {
        if (!register_access_ready() || __initial_count == nullptr)
            return false;

        driver::timer::pit::init_channel0();
        write(REG_TIMER_DIVIDE, TIMER_DIVIDE_BY_16);
        write(REG_LVT_TIMER, LVT_TIMER_MASKED);
        write(REG_TIMER_INITIAL_COUNT, TIMER_CALIBRATION_COUNT);

        const uint16_t start = driver::timer::pit::read_channel0_counter();
        uint32_t elapsed = 0;
        for (uint32_t i = 0; i < PIT_CALIBRATION_TIMEOUT; i++) {
            const uint16_t current = driver::timer::pit::read_channel0_counter();
            elapsed = start >= current ? (uint32_t)(start - current) :
                                         (uint32_t)start + driver::timer::pit::CHANNEL0_DIVISOR - current;
            if (elapsed >= PIT_CALIBRATION_TICKS)
                break;
            asm volatile("pause" : : : "memory");
        }

        const uint32_t current_count = read(REG_TIMER_CURRENT_COUNT);
        write(REG_TIMER_INITIAL_COUNT, 0);
        if (elapsed < PIT_CALIBRATION_TICKS || current_count >= TIMER_CALIBRATION_COUNT)
            return false;

        const uint32_t sampled_apic_ticks = TIMER_CALIBRATION_COUNT - current_count;
        g_timer_initial_count = sampled_apic_ticks * (driver::timer::pit::CHANNEL0_DIVISOR / PIT_CALIBRATION_TICKS);
        *__initial_count = g_timer_initial_count;
        return *__initial_count != 0;
    }

    bool configure_timer(uint8_t __vector, uint32_t __initial_count, bool __periodic) noexcept {
        if (!register_access_ready() || __initial_count == 0)
            return false;

        bigos::serial_puts("BIGOS_LAPIC_TIMER_DIVIDE\n");
        write(REG_TIMER_DIVIDE, TIMER_DIVIDE_BY_16);
        uint32_t lvt = __vector;
        if (__periodic)
            lvt |= LVT_TIMER_PERIODIC;
        bigos::serial_puts("BIGOS_LAPIC_TIMER_LVT\n");
        write(REG_LVT_TIMER, lvt);
        bigos::serial_puts("BIGOS_LAPIC_TIMER_INITIAL\n");
        write(REG_TIMER_INITIAL_COUNT, __initial_count);
        bigos::serial_puts("BIGOS_LAPIC_TIMER_CONFIGURED\n");
        return true;
    }

    uint32_t timer_initial_count() noexcept {
        return g_timer_initial_count;
    }

    void disable_timer() noexcept {
        if (!register_access_ready())
            return;
        write(REG_LVT_TIMER, LVT_TIMER_MASKED);
        write(REG_TIMER_INITIAL_COUNT, 0);
    }
}   // namespace irqchip::lapic
NAMESPACE_DRIVER_END
