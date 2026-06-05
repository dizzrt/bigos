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
#include <bigos/tty.h>
#include <irq/interrupt.h>

#include <bigos/io.h>
#include <ktl/buffer.h>

extern "C" void kernel(const BootInfoHeader *boot_info);

void kernel(const BootInfoHeader *boot_info) {
    driver::video::vga::clear_screen();
    bigos::serial_init();

    bigos::init_mem(boot_info);
#ifdef BIGOS_MM_SELF_TEST
    bigos::mm::self_test();
#endif
    bigos::terminal::init_tty();
    bigos::irq::initIRQ();
#ifdef BIGOS_PAGE_FAULT_SMOKE
    bigos::irq::triggerPageFaultForValidation();
#endif
    bigos::irq::enableIRQ();

    bigos::serial_puts("BigOS kernel reached\n");
    bigos::kprintf("BigOS kernel reached\n");
    while (true) {
        asm volatile("hlt");
    }
}
