#include <bigos/user_mode.h>

#include <string.h>
#include <irq/interrupt.h>

namespace {
    struct GDTPointer {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed));

    struct TSS64 {
        uint32_t reserved0;
        uint64_t rsp0;
        uint64_t rsp1;
        uint64_t rsp2;
        uint64_t reserved1;
        uint64_t ist[7];
        uint64_t reserved2;
        uint16_t reserved3;
        uint16_t io_map_base;
    } __attribute__((packed));

    constexpr uint64_t GDT_KERNEL_CODE = 0x00af9a000000ffffull;
    constexpr uint64_t GDT_KERNEL_DATA = 0x00af92000000ffffull;
    constexpr uint64_t GDT_USER_DATA = 0x00aff2000000ffffull;
    constexpr uint64_t GDT_USER_CODE = 0x00affa000000ffffull;

    struct CpuModeState {
        alignas(16) uint64_t gdt[8];
        TSS64 tss;
        bool initialized;
    };

    CpuModeState g_cpu_mode[bigos::cpu::MAX_CPUS];

    extern "C" void bigos_x86_load_gdt(const GDTPointer *__ptr) noexcept;
    extern "C" void bigos_x86_load_tss(uint16_t __selector) noexcept;
    extern "C" [[noreturn]] void bigos_x86_iret_to_user(
        uint64_t __rip, uint64_t __rsp, uint64_t __cs, uint64_t __ss) noexcept;
    extern "C" [[noreturn]] void bigos_x86_iret_to_user_frame(const bigos::irq::InterruptFrame *__frame) noexcept;

    void set_tss_descriptor(uint64_t *__gdt, uint16_t __selector, const TSS64 *__tss) noexcept {
        const uint64_t base = (uint64_t)__tss;
        const uint32_t limit = sizeof(TSS64) - 1;
        const uint32_t index = __selector >> 3;

        __gdt[index] = ((uint64_t)(limit & 0xffff)) | ((base & 0xffffffull) << 16) | (0x89ull << 40) |
                       (((uint64_t)(limit >> 16) & 0xfull) << 48) | (((base >> 24) & 0xffull) << 56);
        __gdt[index + 1] = base >> 32;
    }

    CpuModeState &mode_state_for(bigos::cpu::CpuId __cpu_id) noexcept {
        if (!bigos::cpu::cpu_id_supported(__cpu_id))
            bigos::cpu::state_for(__cpu_id);
        return g_cpu_mode[__cpu_id];
    }
}   // namespace

namespace bigos::arch::x86 {
    void init_cpu_mode(bigos::cpu::CpuId __cpu_id) noexcept {
        CpuModeState &state = mode_state_for(__cpu_id);
        memset(&state, 0, sizeof(state));
        state.tss.io_map_base = sizeof(state.tss);

        state.gdt[0] = 0;
        state.gdt[KERNEL_CODE_SELECTOR >> 3] = GDT_KERNEL_CODE;
        state.gdt[KERNEL_DATA_SELECTOR >> 3] = GDT_KERNEL_DATA;
        state.gdt[KERNEL_STACK_SELECTOR >> 3] = GDT_KERNEL_DATA;
        state.gdt[USER_DATA_SELECTOR >> 3] = GDT_USER_DATA;
        state.gdt[USER_CODE_SELECTOR >> 3] = GDT_USER_CODE;
        set_tss_descriptor(state.gdt, TSS_SELECTOR, &state.tss);

        const GDTPointer gdt_ptr = {(uint16_t)(sizeof(state.gdt) - 1), (uint64_t)&state.gdt[0]};
        bigos_x86_load_gdt(&gdt_ptr);
        bigos_x86_load_tss(TSS_SELECTOR);
        state.initialized = true;
    }

    void init_user_mode() noexcept {
        init_cpu_mode(bigos::cpu::current_cpu_id());
    }

    void set_cpu_tss_rsp0(bigos::cpu::CpuId __cpu_id, uint64_t __rsp0) noexcept {
        CpuModeState &state = mode_state_for(__cpu_id);
        if (!state.initialized)
            init_cpu_mode(__cpu_id);
        state.tss.rsp0 = __rsp0;
    }

    void set_tss_rsp0(uint64_t __rsp0) noexcept {
        set_cpu_tss_rsp0(bigos::cpu::current_cpu_id(), __rsp0);
    }

    [[noreturn]] void enter_user_mode(uint64_t __rip, uint64_t __rsp) noexcept {
        bigos_x86_iret_to_user(__rip, __rsp, USER_CODE_SELECTOR, USER_DATA_SELECTOR);
    }

    [[noreturn]] void enter_user_mode_frame(const bigos::irq::InterruptFrame *__frame) noexcept {
        bigos_x86_iret_to_user_frame(__frame);
    }
}   // namespace bigos::arch::x86
