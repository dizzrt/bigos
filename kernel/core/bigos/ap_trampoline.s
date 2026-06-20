.file "ap_trampoline.s"

.section .ap_trampoline, "a"
.balign 16
.globl bigos_x86_ap_trampoline_start
.globl bigos_x86_ap_trampoline_end

.set TRAMPOLINE_PHYS, 0x7000
.set MAILBOX_PHYS, 0x8000
.set MAILBOX_KERNEL_CR3, 24
.set MAILBOX_AP_STACK_TOP, 60
.set MAILBOX_ENTRY_POINT, 68

.code16
bigos_x86_ap_trampoline_start:
    cli
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0xff0, %sp

    lgdt gdt_ptr - bigos_x86_ap_trampoline_start

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    .byte 0x66, 0xea
    .long TRAMPOLINE_PHYS + protected_entry - bigos_x86_ap_trampoline_start
    .word 0x08

.code32
protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl MAILBOX_PHYS + MAILBOX_KERNEL_CR3, %eax
    movl %eax, %cr3

    movl $0xc0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    .byte 0xea
    .long TRAMPOLINE_PHYS + long_entry - bigos_x86_ap_trampoline_start
    .word 0x18

.code64
long_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movabsq $(MAILBOX_PHYS + MAILBOX_AP_STACK_TOP), %rax
    movq (%rax), %rsp
    movabsq $MAILBOX_PHYS, %rdi
    movabsq $(MAILBOX_PHYS + MAILBOX_ENTRY_POINT), %rax
    movq (%rax), %rax
    jmp *%rax

.balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00af92000000ffff
    .quad 0x00af9a000000ffff
gdt_end:

gdt_ptr:
    .word gdt_end - gdt - 1
    .long TRAMPOLINE_PHYS + gdt - bigos_x86_ap_trampoline_start

bigos_x86_ap_trampoline_end:
