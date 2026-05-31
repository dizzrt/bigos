// cpp version check
#if __cplusplus < 201703L
#warning C++ 17 is recommended
#endif

#ifndef __GNUC__
#warning It is recommended to build with GCC
#endif

#include <video/vga.h>

#include <memory.h>
#include <irq/interrupt.h>

#include <bigos/io.h>
#include <ktl/buffer.h>

extern "C" void kernel();

void kernel() {
    driver::video::vga::clear_screen();

    bigos::init_mem();
    bigos::irq::initIRQ();
    // bigos::terminal::init_tty();
    bigos::irq::enableIRQ();

    while (true) {
        asm volatile("hlt");
    }
}
