#include <arch/x86/ap_startup.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <bigos/sched.h>
#include <bigos/user_mode.h>
#include <drivers/irqchip/lapic.h>
#include <irq/interrupt.h>
#include <string.h>

NAMESPACE_BIGOS_BEG
namespace arch::x86::ap_startup {
    namespace {
        extern "C" const uint8_t bigos_x86_ap_trampoline_start[];
        extern "C" const uint8_t bigos_x86_ap_trampoline_end[];

        constexpr uint32_t AP_STACK_PAGES = 1;
        constexpr uint32_t STARTUP_TIMEOUT_ITERATIONS = 100000;
        constexpr gfm_t GFM_PRE_PAGING = 1u << 4;
        constexpr uint8_t SIPI_VECTOR = TRAMPOLINE_PHYS >> 12;

        PrepareState g_prepare_state = PrepareState::Unprepared;
        StartupMailbox *g_mailbox = nullptr;

        uint64_t read_cr3() noexcept {
            uint64_t cr3;
            asm volatile("movq %%cr3, %0" : "=r"(cr3));
            return cr3;
        }

        void write_cr3(uint64_t __cr3) noexcept {
            asm volatile("movq %0, %%cr3" : : "r"(__cr3) : "memory");
        }

        void store_gdtr(DescriptorPointer64 *__out) noexcept {
            asm volatile("sgdt %0" : "=m"(*__out) : : "memory");
        }

        void store_idtr(DescriptorPointer64 *__out) noexcept {
            asm volatile("sidt %0" : "=m"(*__out) : : "memory");
        }

        void *low_phys_to_writable(uint32_t __phys, uint32_t __len) noexcept {
            void *direct = bigos::mm::phys_to_direct(__phys);
            if (direct != nullptr && bigos::mm::is_direct_mapped_phys(__phys, __len))
                return direct;
            return reinterpret_cast<void *>(static_cast<uintptr_t>(__phys));
        }

        void load_idt(const DescriptorPointer64 *__idtr) noexcept {
            asm volatile("lidt %0" : : "m"(*__idtr) : "memory");
        }

        [[noreturn]] void park_ap(bool __interrupts_enabled) noexcept {
            for (;;) {
                if (__interrupts_enabled)
                    asm volatile("sti; hlt" : : : "memory");
                else
                    asm volatile("cli; hlt" : : : "memory");
            }
        }

        bool mailbox_valid(const StartupMailbox *__mailbox) noexcept {
            return __mailbox != nullptr && __mailbox->magic == MAILBOX_MAGIC &&
                   __mailbox->version == MAILBOX_VERSION && __mailbox->size == sizeof(StartupMailbox);
        }

        void startup_delay() noexcept {
            for (uint32_t i = 0; i < 10000; i++)
                asm volatile("pause" : : : "memory");
        }
    }   // namespace

    bool prepare_trampoline_region() noexcept {
        static_assert(sizeof(StartupMailbox) <= MAILBOX_SIZE);
        const size_t trampoline_blob_size =
            (size_t)(bigos_x86_ap_trampoline_end - bigos_x86_ap_trampoline_start);
        if (trampoline_blob_size == 0 || trampoline_blob_size > TRAMPOLINE_SIZE) {
            g_prepare_state = PrepareState::TrampolineTooLarge;
            return false;
        }

        void *trampoline = low_phys_to_writable(TRAMPOLINE_PHYS, TRAMPOLINE_SIZE);
        void *mailbox_page = low_phys_to_writable(MAILBOX_PHYS, MAILBOX_SIZE);
        if (trampoline == nullptr || mailbox_page == nullptr) {
            g_prepare_state = PrepareState::InvalidDirectMap;
            return false;
        }

        memset(trampoline, 0, TRAMPOLINE_SIZE);
        memcpy(trampoline, bigos_x86_ap_trampoline_start, trampoline_blob_size);
        memset(mailbox_page, 0, MAILBOX_SIZE);

        g_mailbox = static_cast<StartupMailbox *>(mailbox_page);
        g_mailbox->magic = MAILBOX_MAGIC;
        g_mailbox->version = MAILBOX_VERSION;
        g_mailbox->size = sizeof(StartupMailbox);
        g_mailbox->cpu_id = INVALID_CPU_ID;
        g_mailbox->apic_id = 0xffffffffu;
        g_mailbox->kernel_cr3 = read_cr3();
        store_gdtr(&g_mailbox->gdtr);
        store_idtr(&g_mailbox->idtr);
        g_mailbox->entry_point = (uint64_t)&bigos_x86_ap_kernel_entry;
        g_mailbox->trampoline_phys = TRAMPOLINE_PHYS;
        g_mailbox->trampoline_size = trampoline_blob_size;
        g_prepare_state = PrepareState::Prepared;
        return true;
    }

    bool prepare_startup_mailbox(uint32_t __cpu_id, uint32_t __apic_id, uint64_t __ap_stack_top,
        uint64_t __entry_point, uint64_t __tss_rsp0) noexcept {
        if (g_mailbox == nullptr && !prepare_trampoline_region())
            return false;
        if (g_mailbox == nullptr) {
            g_prepare_state = PrepareState::InvalidDirectMap;
            return false;
        }

        g_mailbox->cpu_id = __cpu_id;
        g_mailbox->apic_id = __apic_id;
        g_mailbox->kernel_cr3 = read_cr3();
        store_gdtr(&g_mailbox->gdtr);
        store_idtr(&g_mailbox->idtr);
        g_mailbox->tss_rsp0 = __tss_rsp0;
        g_mailbox->ap_stack_top = __ap_stack_top;
        g_mailbox->entry_point = __entry_point != 0 ? __entry_point : (uint64_t)&bigos_x86_ap_kernel_entry;
        g_mailbox->ack_state = ACK_UNSTARTED;
        g_prepare_state = PrepareState::Prepared;
        return true;
    }

    WaitResult wait_for_online_ack(uint32_t __cpu_id, uint32_t __timeout_iterations) noexcept {
        if (g_mailbox == nullptr)
            return WaitResult::InvalidMailbox;
        if (!bigos::cpu::cpu_id_supported(__cpu_id))
            return WaitResult::InvalidCpu;

        volatile StartupMailbox *mailbox = g_mailbox;
        for (uint32_t i = 0; i < __timeout_iterations; i++) {
            if (mailbox->ack_state == ACK_ONLINE && bigos::cpu::cpu_online(__cpu_id))
                return WaitResult::Online;
            if (mailbox->ack_state == ACK_INVALID_CPU)
                return WaitResult::InvalidCpu;
            if (mailbox->ack_state == ACK_INVALID_MAILBOX)
                return WaitResult::InvalidMailbox;
            asm volatile("pause" : : : "memory");
        }

        (void)bigos::cpu::mark_cpu_failed(__cpu_id, bigos::cpu::CpuFailureReason::StartupTimeout);
        return WaitResult::Timeout;
    }

    bool init_local_timer_for_cpu(uint32_t __cpu_id) noexcept {
        if (!bigos::cpu::cpu_online(__cpu_id))
            return false;

        const uint32_t initial_count = driver::irqchip::lapic::timer_initial_count();
        if (initial_count == 0) {
            bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_NO_CALIBRATION\n");
            (void)bigos::cpu::set_local_timer_state(__cpu_id, bigos::cpu::LocalTimerState::Failed);
            return false;
        }
        bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_INIT\n");
        if (!driver::irqchip::lapic::init()) {
            bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_LAPIC_FAILED\n");
            (void)bigos::cpu::set_local_timer_state(__cpu_id, bigos::cpu::LocalTimerState::Failed);
            return false;
        }
        bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_CONFIG\n");
        if (!driver::irqchip::lapic::configure_timer(bigos::irq::VECTOR_LAPIC_TIMER, initial_count, true)) {
            bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_FAILED\n");
            (void)bigos::cpu::set_local_timer_state(__cpu_id, bigos::cpu::LocalTimerState::Failed);
            return false;
        }

        bigos::serial_puts("BIGOS_AP_LOCAL_TIMER_READY\n");
        (void)bigos::cpu::set_local_timer_state(__cpu_id, bigos::cpu::LocalTimerState::Ready);
        return true;
    }

    uint32_t start_application_processors() noexcept {
        if (!prepare_trampoline_region()) {
            bigos::serial_puts("BIGOS_AP_TRAMPOLINE_FAILED\n");
            return 0;
        }
        if (!driver::irqchip::lapic::init()) {
            if (driver::irqchip::lapic::status() == driver::irqchip::lapic::Status::Unavailable)
                bigos::serial_puts("BIGOS_AP_LAPIC_UNAVAILABLE\n");
            else if (driver::irqchip::lapic::status() == driver::irqchip::lapic::Status::DirectMapUnavailable)
                bigos::serial_puts("BIGOS_AP_LAPIC_MMIO_FAILED\n");
            else
                bigos::serial_puts("BIGOS_AP_LAPIC_FAILED\n");
            return 0;
        }

        uint32_t started = 0;
        const bigos::cpu::CpuId discovered = bigos::cpu::discovered_cpu_count();
        bigos::serial_puts("BIGOS_AP_START_TRY\n");
        if (discovered <= 1)
            bigos::serial_puts("BIGOS_AP_NONE\n");
        for (bigos::cpu::CpuId id = 1; id < discovered; id++) {
            const bigos::cpu::CpuSlot &slot = bigos::cpu::slot_for(id);
            if (slot.startup_state != bigos::cpu::CpuStartupState::Offline)
                continue;

            void *stack = bigos::alloc_kernel_pages(AP_STACK_PAGES, GFM_PRE_PAGING);
            if (stack == nullptr) {
                bigos::serial_puts("BIGOS_AP_STACK_FAILED\n");
                (void)bigos::cpu::mark_cpu_failed(id, bigos::cpu::CpuFailureReason::CapacityExceeded);
                continue;
            }

            const uint64_t stack_top = (uint64_t)stack + AP_STACK_PAGES * 4096ull;
            if (!prepare_startup_mailbox(id, slot.apic_id, stack_top, (uint64_t)&bigos_x86_ap_kernel_entry, stack_top)) {
                bigos::serial_puts("BIGOS_AP_MAILBOX_FAILED\n");
                (void)bigos::cpu::mark_cpu_failed(id, bigos::cpu::CpuFailureReason::InvalidTopology);
                bigos::free_pages(stack);
                continue;
            }

            if (!driver::irqchip::lapic::send_init(slot.apic_id)) {
                bigos::serial_puts("BIGOS_AP_INIT_FAILED\n");
                (void)bigos::cpu::mark_cpu_failed(id, bigos::cpu::CpuFailureReason::ApicUnavailable);
                bigos::free_pages(stack);
                continue;
            }
            bigos::serial_puts("BIGOS_AP_INIT_SENT\n");
            startup_delay();
            if (!driver::irqchip::lapic::send_sipi(slot.apic_id, SIPI_VECTOR)) {
                bigos::serial_puts("BIGOS_AP_SIPI_FAILED\n");
                (void)bigos::cpu::mark_cpu_failed(id, bigos::cpu::CpuFailureReason::ApicUnavailable);
                bigos::free_pages(stack);
                continue;
            }
            bigos::serial_puts("BIGOS_AP_SIPI_SENT\n");

            const WaitResult wait_result = wait_for_online_ack(id, STARTUP_TIMEOUT_ITERATIONS);
            if (wait_result == WaitResult::Online) {
                bigos::serial_puts("BIGOS_AP_ONLINE\n");
                started++;
            } else if (wait_result == WaitResult::InvalidCpu) {
                bigos::serial_puts("BIGOS_AP_INVALID_CPU\n");
            } else if (wait_result == WaitResult::InvalidMailbox) {
                bigos::serial_puts("BIGOS_AP_INVALID_MAILBOX\n");
            } else {
                bigos::serial_puts("BIGOS_AP_TIMEOUT\n");
            }
        }
        return started;
    }

    PrepareState prepare_state() noexcept {
        return g_prepare_state;
    }

    const StartupMailbox *mailbox() noexcept {
        return g_mailbox;
    }
}   // namespace arch::x86::ap_startup
NAMESPACE_BIGOS_END

extern "C" [[noreturn]] void bigos_x86_ap_kernel_entry(
    bigos::arch::x86::ap_startup::StartupMailbox *__mailbox) noexcept {
    using namespace bigos::arch::x86::ap_startup;

    bigos::serial_puts("BIGOS_AP_ENTRY\n");
    if (!mailbox_valid(__mailbox)) {
        if (__mailbox != nullptr)
            __mailbox->ack_state = ACK_INVALID_MAILBOX;
        park_ap(false);
    }
    bigos::serial_puts("BIGOS_AP_MAILBOX_OK\n");

    const bigos::cpu::CpuId cpu_id = __mailbox->cpu_id;
    if (!bigos::cpu::cpu_id_supported(cpu_id) || !bigos::cpu::mark_cpu_online(cpu_id, __mailbox->apic_id)) {
        __mailbox->ack_state = ACK_INVALID_CPU;
        park_ap(false);
    }
    bigos::serial_puts("BIGOS_AP_CPU_BOUND\n");

    write_cr3(__mailbox->kernel_cr3);
    load_idt(&__mailbox->idtr);
    bigos::serial_puts("BIGOS_AP_IDT_READY\n");
    bigos::arch::x86::init_cpu_mode(cpu_id);
    bigos::serial_puts("BIGOS_AP_GDT_READY\n");
    bigos::arch::x86::set_cpu_tss_rsp0(cpu_id, __mailbox->tss_rsp0 != 0 ? __mailbox->tss_rsp0 : __mailbox->ap_stack_top);

    bigos::cpu::LocalState &local = bigos::cpu::state_for(cpu_id);
    local.current_thread = nullptr;
    local.current_process = nullptr;
    local.current_address_space_root = __mailbox->kernel_cr3;
    local.irq_nesting_depth = 0;
    local.nonblocking_depth = 0;
    local.preemption_disable_depth = 0;
    local.reschedule_pending = false;
    bigos::serial_puts("BIGOS_AP_LOCAL_STATE_READY\n");

    if (!bigos::arch::x86::ap_startup::init_local_timer_for_cpu(cpu_id)) {
        bigos::serial_puts("BIGOS_AP_TIMER_FAILED\n");
        (void)bigos::cpu::mark_cpu_failed(cpu_id, bigos::cpu::CpuFailureReason::TimerUnavailable);
        __mailbox->ack_state = ACK_INVALID_CPU;
        park_ap(false);
    }

    if (!bigos::sched::init_current_cpu_domain()) {
        __mailbox->ack_state = ACK_INVALID_CPU;
        park_ap(false);
    }
    bigos::serial_puts("BIGOS_AP_SCHED_READY\n");

    __mailbox->ack_state = ACK_ONLINE;
    bigos::irq::enableIRQ();
    bigos::sched::start();
    park_ap(false);
}
