# BigOS user-space C runtime startup (crt0).
#
# Real ELF entry point for user C programs. The kernel ELF loader plus
# copy_exec_args_to_stack place the initial user stack as:
#
#   [ argc ][ argv[0] ... argv[argc-1] ][ NULL ][ envp[0] ... ][ NULL ][ strings ]
#    ^ initial SP
#
# crt0 reads argc/argv/envp from that layout, records environ for getenv,
# aligns the stack to the System V 16-byte boundary, and calls
# main(argc, argv, envp). When main returns its value is passed to exit(), which
# flushes buffered libc streams before issuing SYS_EXIT. crt0 never returns to an
# undefined address.

.section .text
.global _start
_start:
    xor %rbp, %rbp                 # mark the outermost frame

    movq (%rsp), %rdi              # rdi = argc
    leaq 8(%rsp), %rsi             # rsi = argv (array starts just above argc)
    leaq 8(%rsi,%rdi,8), %rdx      # rdx = envp = argv + (argc + 1) * 8

    movq %rdx, environ(%rip)       # publish environ for getenv()

    andq $-16, %rsp                # align stack to 16 bytes before call

    call main                      # main(argc, argv, envp); return value in eax

    movl %eax, %edi                # exit(main_return); exit() flushes stdio
    call exit
1:
    hlt                            # exit must not return; halt defensively
    jmp 1b
