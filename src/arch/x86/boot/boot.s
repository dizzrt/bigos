.file "boot.s"
.code32
.text

.globl _start
_start:
    jmp _boot

    gdt_32:
    .quad 0x0000000000000000 # zero
    .quad 0x00cf98000000ffff # code
    .quad 0x00cf92000000ffff # data
    .quad 0x0040960900000000 # stack
    .quad 0x00af98000000ffff # code_64

    .equ SELECTOR_CODE_32, 0x01<<3
    .equ SELECTOR_DATA_32, 0x02<<3
    .equ SELECTOR_STACK_32, 0x03<<3
    .equ SELECTOR_CODE64_32, 0x04<<3

gdt_attribute_32:
    .word 0xff
    .quad gdt_32

gdt_attribute:
    .word 0x03ff
    .quad 0x0000

idt_attribute:
    .word 0x0fff
    .quad 0x1000

_boot:
    # Keep interrupts masked until the kernel installs its own IDT.
    cli
    lgdt (gdt_attribute_32)
    ljmp $SELECTOR_CODE_32, $reload_gdt

reload_gdt:
    # prepare register and stack
    movw $SELECTOR_DATA_32, %ax
    movw %ax, %ds
    movw %ax, %es

    movw $SELECTOR_STACK_32, %ax
    movw %ax, %ss
    movl $0xfbff, %esp

    # build gdt for long mode
    movl $0, %ebx
    movl $0x00000000, 0x00(%ebx) #zero
    movl $0x00000000, 0x04(%ebx)
    movl $0x0000ffff, 0x08(%ebx) #code
    movl $0x00af9a00, 0x0c(%ebx)
    movl $0x0000ffff, 0x10(%ebx) #data
    movl $0x00af9200, 0x14(%ebx)
    movl $0x0000ffff, 0x18(%ebx) #stack
    movl $0x00af9600, 0x1c(%ebx)

    .equ SELECTOR_CODE, 0x01<<3
    .equ SELECTOR_DATA, 0x02<<3
    .equ SELECTOR_STACK, 0x03<<3

    # prepare mem for paging
    mov $0x2000, %ebx
    mov $0x1400, %ecx
clear_mem_loop:
    movl $0, (%ebx)
    addl $0x04, %ebx
    loop clear_mem_loop

    # setup paging
    movl $0x00003003, (0x2000) # pml4t[0]
    movl $0x00000000, (0x2004)
    movl $0x00003003, (0x2ff8) # pml4t[511]
    movl $0x00000000, (0x2ffc)
    movl $0x00002003, (0x2800) # pmlt[256] self-mapping

    movl $0x00004003, (0x3000) # pdpt[0]
    movl $0x00000000, (0x3004)
    movl $0x00005003, (0x3ff0) # pdpt[510]
    movl $0x00000000, (0x3ff4)

    movl $0x00006003, (0x4000) # pd_1[0]
    movl $0x00000000, (0x4004)

    # pt - map to lowest 2MB
    movl $0x6000, %ebx
    movl $0x00000003, %edx
    movl $0x200, %ecx
build_pt_loop:
    movl %edx, 0x00(%ebx)
    movl $0x00000000, 0x04(%ebx)
    addl $0x1000, %edx
    addl $0x08, %ebx
    loop build_pt_loop

    # open paging & switch to compatibility mode
    # cr3 saves the physical address of pml4 in 4k alignment
    movl %cr3, %eax
    orl $0x2000, %eax
    movl %eax, %cr3

    # IA32_EFER(0xc0000080)
    movl $0xc0000080, %ecx
    rdmsr
    # set IA32_EFER.LME = 1
    bts $0x08, %eax
    wrmsr

    # set cr4.PAE = 1
    movl %cr4, %eax
    bts $0x05, %eax
    movl %eax, %cr4

    # set cr0.PG = 1
    movl %cr0, %eax
    bts $0x1f, %eax
    movl %eax, %cr0

    ljmp $SELECTOR_CODE64_32, $long_gate

.code64
long_gate:
    # now we are in compatibility mode
    # switch to long mode
    pushf                       # flags
    pushq $SELECTOR_CODE        # cs
    pushq $long_mode            # ip
    lgdt (gdt_attribute)
    iretq

.extern boot
.extern g_boot_handoff_address
long_mode:
    lidt (idt_attribute)

    # prepare registers and stack for long mode
    # Long mode ignores most segmentation, but QEMU still faults stack writes
    # when SS is loaded from the legacy expand-down stack descriptor.
    mov $SELECTOR_DATA, %ax
    mov %ax, %ss
    movq $0x9fbff, %rsp

    mov $SELECTOR_DATA, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # call boot function to load kernel, setup paging, and return a checked ELF entry
    call boot

    # Pass BootInfoHeader* in the x86_64 first argument register.
    movq g_boot_handoff_address(%rip), %rdi

    # jmp to kernel
    jmp *%rax

.globl wait_nops
wait_nops:
    .rept 8
        nop
    .endr
    ret
