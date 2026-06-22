#include <stdarg.h>
#include <string.h>
#include <bigos/arch_vm_user_boundary.h>
#include <bigos/device.h>
#include <bigos/io.h>
#include <bigos/proc.h>
#include <bigos/utils.h>

#define COM1_PORT 0x3f8
#define COM1_LINE_STATUS 5
#define COM1_TRANSMIT_EMPTY 0x20

uint8_t bigos::inb(uint16_t port) {
    uint8_t ret;

    asm volatile("inb %w1,%b0" : "=a"(ret) : "d"(port));
    return ret;
}

uint16_t bigos::inw(uint16_t port) {
    uint16_t ret;

    asm volatile("inw %w1,%w0" : "=a"(ret) : "d"(port));
    return ret;
}

void bigos::outb(uint16_t port, uint8_t value) {
    asm volatile("outb %b1,%w0" : : "d"(port), "a"(value));
    return;
}

void bigos::outw(uint16_t port, uint16_t value) {
    asm volatile("outw %w1,%w0" : : "d"(port), "a"(value));
    return;
}

void bigos::serial_init() {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xc7);
    outb(COM1_PORT + 4, 0x0b);
}

void bigos::serial_puts(const char *s) {
    if (s == nullptr)
        return;

    while (*s != 0) {
        for (uint32_t i = 0; i < 100000; i++) {
            if ((inb(COM1_PORT + COM1_LINE_STATUS) & COM1_TRANSMIT_EMPTY) != 0)
                break;
        }
        outb(COM1_PORT, (uint8_t)*s);
        s++;
    }
}

namespace {
    uint64_t switch_to_current_kernel_cr3() {
#ifdef BIGOS_USER_PROCESS
        const bigos::proc::Process *process = bigos::proc::current_process();
        if (process == nullptr || process->kernel_address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            return 0;

        const uint64_t active_root = bigos::arch::vm_user::active_address_space_root();
        if (bigos::arch::vm_user::is_active_address_space(process->kernel_address_space_root))
            return 0;

        bigos::arch::vm_user::activate_kernel_address_space(process->kernel_address_space_root);
        return active_root;
#else
        return 0;
#endif
    }

    void restore_cr3(uint64_t root) {
#ifdef BIGOS_USER_PROCESS
        if (root != 0 && !bigos::arch::vm_user::is_active_address_space(root))
            bigos::arch::vm_user::activate_address_space(root);
#else
        (void)root;
#endif
    }
}   // namespace

void bigos::kput(char c) {
    const uint64_t restore_root = switch_to_current_kernel_cr3();
    bigos::device::write_video_text(c);
    restore_cr3(restore_root);
}

void bigos::kputs(const char *s) {
    const uint64_t restore_root = switch_to_current_kernel_cr3();
    bigos::device::write_video_text(s);
    restore_cr3(restore_root);
}

static void emit_buffer(const char *buffer, bool dual) {
    bigos::kputs(buffer);
    if (dual)
        bigos::serial_puts(buffer);
}

static uint8_t buffer_append(char *buffer, char *str, uint32_t offset, bool dual) {
    uint32_t soffset = 0;
    uint32_t slen = strlen(str);
    uint32_t ret = (offset + slen) % 256;

    while (slen > 0xff - offset) {
        uint32_t len = 0xff - offset;
        memcpy(buffer, str + soffset, len);
        emit_buffer(buffer, dual);
        memset(buffer, 0, sizeof(char) * 256);

        slen -= len;
        soffset += len;
    }

    if (slen > 0)
        memcpy(buffer + offset, str + soffset, slen);

    return ret;
}

static void kvprintf_impl(const char *fmt, va_list vlist, bool dual) {
    char buffer[256];
    uint32_t offset_buf = 0;
    memset(buffer, 0, sizeof(char) * 256);

    while (*fmt != 0) {
        if (fmt[0] == '%') {
            if (fmt[1] == 'c') {
                char c[2] = {0};
                c[0] = va_arg(vlist, int);
                offset_buf = buffer_append(buffer, c, offset_buf, dual);
            } else if (fmt[1] == 's') {
                char *s = (char *)va_arg(vlist, long long);
                offset_buf = buffer_append(buffer, s, offset_buf, dual);
            } else {
                // may be a number
                bool is_long = false;
                char nbuffer[32] = {0};
                char *nbuffer_ptr = (char *)&nbuffer;

                if (fmt[1] == 'l' && fmt[2] == 'l') {
                    is_long = true;
                    fmt += 2;
                }

                int radix = 10;
                switch (fmt[1]) {
                    case 'd':
                        break;
                    case 'x':
                        radix = 16;
                        nbuffer_ptr[0] = '0';
                        nbuffer_ptr[1] = 'x';
                        nbuffer_ptr += 2;
                        break;
                    default:
                        fmt++;
                        goto NORMAL_CHAR;
                        break;
                }

                if (is_long)
                    bigos::utoa(va_arg(vlist, uint64_t), nbuffer_ptr, radix);
                else
                    bigos::utoa(va_arg(vlist, uint32_t), nbuffer_ptr, radix);
                offset_buf = buffer_append(buffer, nbuffer, offset_buf, dual);
            }

            fmt += 2;
        } else {
        NORMAL_CHAR:
            if (offset_buf < 255)
                buffer[offset_buf++] = *fmt++;
            else {
                emit_buffer(buffer, dual);
                memset(buffer, 0, sizeof(char) * 256);
                buffer[0] = *fmt++;
                offset_buf = 0;
            }
        }
    }

    if (offset_buf > 0)
        emit_buffer(buffer, dual);
}

void bigos::kprintf(const char *fmt, ...) {
    va_list vlist;
    va_start(vlist, fmt);
    kvprintf_impl(fmt, vlist, false);
    va_end(vlist);
}

void bigos::kvprintf_dual(const char *fmt, va_list args) {
    kvprintf_impl(fmt, args, true);
}
