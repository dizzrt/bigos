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

    alignas(16) uint64_t g_gdt[8];
    TSS64 g_tss;

    extern "C" void bigos_x86_load_gdt(const GDTPointer *__ptr) noexcept;
    extern "C" void bigos_x86_load_tss(uint16_t __selector) noexcept;
    extern "C" [[noreturn]] void bigos_x86_iret_to_user(
        uint64_t __rip, uint64_t __rsp, uint64_t __cs, uint64_t __ss) noexcept;
    extern "C" [[noreturn]] void bigos_x86_iret_to_user_frame(const bigos::irq::InterruptFrame *__frame) noexcept;

    void set_tss_descriptor(uint16_t __selector, const TSS64 *__tss) noexcept {
        const uint64_t base = (uint64_t)__tss;
        const uint32_t limit = sizeof(TSS64) - 1;
        const uint32_t index = __selector >> 3;

        g_gdt[index] = ((uint64_t)(limit & 0xffff)) | ((base & 0xffffffull) << 16) | (0x89ull << 40) |
                       (((uint64_t)(limit >> 16) & 0xfull) << 48) | (((base >> 24) & 0xffull) << 56);
        g_gdt[index + 1] = base >> 32;
    }
}   // namespace

namespace bigos::arch::x86 {
    void init_user_mode() noexcept {
        memset(&g_tss, 0, sizeof(g_tss));
        g_tss.io_map_base = sizeof(g_tss);

        g_gdt[0] = 0;
        g_gdt[KERNEL_CODE_SELECTOR >> 3] = GDT_KERNEL_CODE;
        g_gdt[KERNEL_DATA_SELECTOR >> 3] = GDT_KERNEL_DATA;
        g_gdt[KERNEL_STACK_SELECTOR >> 3] = GDT_KERNEL_DATA;
        g_gdt[USER_DATA_SELECTOR >> 3] = GDT_USER_DATA;
        g_gdt[USER_CODE_SELECTOR >> 3] = GDT_USER_CODE;
        set_tss_descriptor(TSS_SELECTOR, &g_tss);

        const GDTPointer gdt_ptr = {(uint16_t)(sizeof(g_gdt) - 1), (uint64_t)&g_gdt[0]};
        bigos_x86_load_gdt(&gdt_ptr);
        bigos_x86_load_tss(TSS_SELECTOR);
    }

    void set_tss_rsp0(uint64_t __rsp0) noexcept {
        g_tss.rsp0 = __rsp0;
    }

    [[noreturn]] void enter_user_mode(uint64_t __rip, uint64_t __rsp) noexcept {
        bigos_x86_iret_to_user(__rip, __rsp, USER_CODE_SELECTOR, USER_DATA_SELECTOR);
    }

    [[noreturn]] void enter_user_mode_frame(const bigos::irq::InterruptFrame *__frame) noexcept {
        bigos_x86_iret_to_user_frame(__frame);
    }
}   // namespace bigos::arch::x86
