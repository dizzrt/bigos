#include <stdarg.h>

#include <bigos/io.h>
#include <bigos/panic.h>
#include <bigos/utils.h>
#include <bigos/memory.h>

NAMESPACE_BIGOS_BEG

namespace {
    // Emit a constant string to both COM1 and VGA. Constant-only, no formatting.
    void emit_dual(const char *__s) noexcept {
        bigos::serial_puts(__s);
        bigos::kputs(__s);
    }

    // Emit the fixed marker first line. The constant `BIGOS_PANIC` prefix is written
    // via serial_puts before any formatting so the most critical signal lands first.
    void emit_marker(PanicCode __code, const char *__source) noexcept {
        bigos::serial_puts("BIGOS_PANIC ");

        char code_buf[19] = {0};
        bigos::utoa((uint64_t)__code, code_buf, 16);

        emit_dual("BIGOS_PANIC code=0x");
        emit_dual(code_buf);
        emit_dual(" source=");
        emit_dual(__source != nullptr ? __source : "(null)");
        emit_dual("\n");
    }
}   // namespace

void khalt() noexcept {
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

void kpanic(PanicCode __code, const char *__source) noexcept {
    emit_marker(__code, __source);
    khalt();
}

void kpanic(PanicCode __code, const char *__source, const char *__fmt, ...) noexcept {
    emit_marker(__code, __source);

    if (__fmt != nullptr) {
        va_list args;
        va_start(args, __fmt);
        bigos::kvprintf_dual(__fmt, args);
        va_end(args);
    }

    khalt();
}

void kpanic_with_mm_stats(PanicCode __code, const char *__source) noexcept {
    emit_marker(__code, __source);
    // Read-only allocator stats; MUST NOT allocate or trigger failing paths.
    bigos::mm::print_slab_stats();
    khalt();
}

NAMESPACE_BIGOS_END
