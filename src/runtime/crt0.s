.code64
.section .bigos, "ax"
.extern kernel
.extern _init
.extern _fini

.globl _start
.type _start, @function
_start:
    mov %rdi, %r12
    call _init
    mov %r12, %rdi
    call kernel
    call _fini
