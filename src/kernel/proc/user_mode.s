.file "user_mode.s"
.code64

.text
.globl bigos_x86_load_gdt
bigos_x86_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    retq

.globl bigos_x86_load_tss
bigos_x86_load_tss:
    movw %di, %ax
    ltr %ax
    retq

.globl bigos_x86_iret_to_user
bigos_x86_iret_to_user:
    movw %cx, %ax
    movw %ax, %ds
    movw %ax, %es

    pushq %rcx
    pushq %rsi
    pushfq
    orq $0x200, (%rsp)
    pushq %rdx
    pushq %rdi
    iretq
