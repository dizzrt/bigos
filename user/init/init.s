.section .text
.global _start

_start:
    mov $2, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $message_end - message, %rdx
    int $0x80

    mov $3, %rax
    xor %rdi, %rdi
    int $0x80

1:
    hlt
    jmp 1b

.section .rodata
message:
    .ascii "BIGOS_USER_ELF_WRITE\n"
message_end:
