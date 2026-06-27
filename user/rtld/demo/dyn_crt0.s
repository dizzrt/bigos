# PIC crt0 for the BigOS dynamic-link demo executable (dyn_demo).
#
# After ld.so finishes relocation it jumps here (the main image real entry,
# AT_ENTRY) on the untouched System V initial stack:
#
#   [ argc ][ argv... ][ NULL ][ envp... ][ NULL ][ auxv... ]
#    ^ rsp
#
# This reads argc/argv/envp and calls main(argc, argv, envp). Built -fPIC so the
# call to main is position-independent. main never returns control here; it
# exits via SYS_EXIT.

.equ SYS_EXIT, 3

.section .text
.global _start
.hidden _start
_start:
    xor %rbp, %rbp
    movq (%rsp), %rdi              # argc
    leaq 8(%rsp), %rsi             # argv
    leaq 8(%rsi,%rdi,8), %rdx      # envp = argv + (argc + 1) * 8
    andq $-16, %rsp                # 16-byte alignment for the call
    call main@PLT
    movl %eax, %edi
    movq $SYS_EXIT, %rax
    int $0x80
1:
    hlt
    jmp 1b
