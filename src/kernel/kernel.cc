// cpp version check
#if __cplusplus < 201703L
#warning C++ 17 is recommended
#endif

#ifndef __GNUC__
#warning It is recommended to build with GCC
#endif

#include <drivers/video/vga.h>

#include <arch/x86/boot/boot_info.h>
#include <bigos/memory.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/tty.h>
#include <irq/interrupt.h>

#include <bigos/io.h>
#include <ktl/buffer.h>

extern "C" void kernel(const BootInfoHeader *boot_info);

#ifdef BIGOS_SCHEDULER_SMOKE
namespace {
    // Bounded deterministic scheduler smoke: two worker threads emit a fixed
    // number of markers and yield to each other, proving cooperative switching.
    constexpr uint32_t SCHEDULER_SMOKE_ITERATIONS = 3;

    void scheduler_smoke_worker_a(void *) noexcept {
        for (uint32_t i = 0; i < SCHEDULER_SMOKE_ITERATIONS; i++) {
            bigos::serial_puts("BIGOS_SCHED_THREAD_A\n");
            bigos::sched::yield();
        }
    }

    void scheduler_smoke_worker_b(void *) noexcept {
        for (uint32_t i = 0; i < SCHEDULER_SMOKE_ITERATIONS; i++) {
            bigos::serial_puts("BIGOS_SCHED_THREAD_B\n");
            bigos::sched::yield();
        }
    }
}   // namespace
#endif

#ifdef BIGOS_SYSCALL_SMOKE
namespace {
    // Bounded deterministic syscall smoke: from ring0 non-interrupt context,
    // issue a few `int 0x80` syscalls, read the return value from rax, and assert
    // the dispatcher was reached, the return value is correct, and an unknown
    // number returns the deterministic error code. Emits BIGOS_SYSCALL_* markers.
    //
    // This stage triggers int 0x80 from ring0 only: it does not enter ring3,
    // switch CR3, or load any user program.
    constexpr uint64_t SYSCALL_SMOKE_UNKNOWN_NUMBER = 0xdead;

    inline int64_t do_syscall(uint64_t number, uint64_t arg0) noexcept {
        int64_t result;
        // number in rax, argument 0 in rdi (per the minimal syscall ABI). The
        // result is returned in rax. rcx/r11 are clobbered by int/iretq semantics.
        asm volatile("int $0x80" : "=a"(result) : "a"(number), "D"(arg0) : "rcx", "r11", "memory");
        return result;
    }

    void syscall_smoke() noexcept {
        // SYS_DEBUG_WRITE emits the BIGOS_SYSCALL_WRITE marker and returns the
        // number of bytes written (length of the kernel-internal marker buffer).
        const int64_t write_ret = do_syscall(bigos::sys::SYS_DEBUG_WRITE, 0);

        // SYS_GET_TICK returns the monotonic kernel tick via rax.
        const int64_t tick_ret = do_syscall(bigos::sys::SYS_GET_TICK, 0);

        // Unknown number must return the deterministic SYS_ENOSYS error code.
        const int64_t unknown_ret = do_syscall(SYSCALL_SMOKE_UNKNOWN_NUMBER, 0);

        const bool ok = write_ret > 0 && tick_ret >= 0 && unknown_ret == bigos::sys::SYS_ENOSYS;
        if (ok)
            bigos::serial_puts("BIGOS_SYSCALL_SMOKE_PASSED\n");
        else
            bigos::serial_puts("BIGOS_SYSCALL_SMOKE_FAILED\n");
    }
}   // namespace
#endif

void kernel(const BootInfoHeader *boot_info) {
    driver::video::vga::clear_screen();
    bigos::serial_init();

    bigos::init_mem(boot_info);
#ifdef BIGOS_MM_SELF_TEST
    bigos::mm::self_test();
#endif
#ifdef BIGOS_USER_VMEM_SMOKE
    // Non-interrupt-context one-shot validation of the page-attribute primitives
    // and user address-space root derivation. Does not switch CR3 or enter ring3.
    bigos::mm::user_vmem_smoke();
#endif
    bigos::terminal::init_tty();
    bigos::irq::initIRQ();
#ifdef BIGOS_PAGE_FAULT_SMOKE
    bigos::irq::triggerPageFaultForValidation();
#endif
    bigos::irq::enableIRQ();

    bigos::serial_puts("BigOS kernel reached\n");
    bigos::kprintf("BigOS kernel reached\n");

#ifdef BIGOS_SYSCALL_SMOKE
    // Non-interrupt-context one-shot validation of the int 0x80 syscall entry,
    // ABI register convention, dispatch routing, and unknown-number error return.
    // Runs from ring0 only; does not enter ring3 or switch CR3.
    syscall_smoke();
#endif

#ifdef BIGOS_SCHEDULER_SMOKE
    bigos::sched::create_kernel_thread(&scheduler_smoke_worker_a, nullptr);
    bigos::sched::create_kernel_thread(&scheduler_smoke_worker_b, nullptr);
#endif

    // The post-initialization halt behavior is now owned by the scheduler idle
    // thread instead of a naked hlt loop in kernel(). Enter after IRQs are on so
    // timer IRQ0 can wake the idle hlt.
    bigos::sched::start();
}
