.code64
.section .bigos, "ax"
.extern kernel
.extern _init
.extern _fini

.globl _start
.type _start, @function
_start:
    mov %rdi, %r12
    call __early_serial_init
    call _init
    mov %r12, %rdi
    call kernel
    call _fini

__early_serial_init:
    push %rax
    push %rdx
    mov $0x3f9, %dx
    mov $0x00, %al
    out %al, %dx
    mov $0x3fb, %dx
    mov $0x80, %al
    out %al, %dx
    mov $0x3f8, %dx
    mov $0x03, %al
    out %al, %dx
    mov $0x3f9, %dx
    mov $0x00, %al
    out %al, %dx
    mov $0x3fb, %dx
    mov $0x03, %al
    out %al, %dx
    mov $0x3fa, %dx
    mov $0xc7, %al
    out %al, %dx
    mov $0x3fc, %dx
    mov $0x0b, %al
    out %al, %dx
    pop %rdx
    pop %rax
    ret
