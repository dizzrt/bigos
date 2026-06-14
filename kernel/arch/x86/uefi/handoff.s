.text
.globl uefi_handoff
uefi_handoff:
    # Windows x86_64 ABI: rcx=cr3, rdx=stack_top, r8=entry, r9=BootInfoHeader*.
    cli
    movq %rcx, %cr3
    movq %rdx, %rsp
    xorq %rbp, %rbp
    movq %r9, %rdi
    jmp *%r8
