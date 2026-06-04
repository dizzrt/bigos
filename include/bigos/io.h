#ifndef _BIG_IO_H
#define _BIG_IO_H

#include <stdarg.h>

#include <bigos/types.h>

namespace bigos {
    uint8_t inb(uint16_t port);
    uint16_t inw(uint16_t port);
    void outb(uint16_t port, uint8_t value);

    void kput(char c);
    void kputs(const char *s);
    void kprintf(const char *fmt, ...);

    // Formatted output to both VGA and COM1, used by fatal diagnostics.
    void kvprintf_dual(const char *fmt, va_list args);

    void serial_init();
    void serial_puts(const char *s);

}   // namespace bigos

#endif
