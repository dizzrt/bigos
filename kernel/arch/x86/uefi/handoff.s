.text
.globl uefi_handoff
uefi_handoff:
    # Windows x86_64 ABI: rcx=cr3, rdx=stack_top, r8=entry, r9=BootInfoHeader*.
    cli
    movq %rcx, %cr3
    movq %rdx, %rsp
    xorq %rbp, %rbp
    lgdt uefi_gdt_ptr(%rip)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ss
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movq %r9, %rdi
    jmp *%r8

.p2align 4
uefi_gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
    .quad 0x00af96000000ffff
uefi_gdt_end:

uefi_gdt_ptr:
    .word uefi_gdt_end - uefi_gdt - 1
    .quad uefi_gdt
