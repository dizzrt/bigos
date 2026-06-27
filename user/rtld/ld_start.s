# BigOS user-space dynamic linker (ld.so) bare entry.
#
# The kernel enters ring3 here with the System V initial stack:
#
#   [ argc ][ argv... ][ NULL ][ envp... ][ NULL ][ auxv... ][ AT_NULL ]
#    ^ rsp
#
# _dl_start hands the raw stack pointer and the run-time address of its own
# _DYNAMIC to _dl_main, which self-relocates first (using AT_BASE from auxv),
# loads DT_NEEDED objects, applies the bounded relocation subset, and returns
# the main image entry point. _dl_start then restores the untouched initial
# stack pointer and jumps to that entry; the main program crt0 reads
# argc/argv/envp exactly as in the static path.
#
# _DYNAMIC is referenced PC-relative (no GOT, no relocation needed), so the
# very first thing _dl_main can do is self-relocation without depending on any
# yet-unrelocated global.

.section .text
.global _dl_start
.hidden _dl_start
_dl_start:
    xor %rbp, %rbp                 # outermost frame
    mov %rsp, %r15                 # preserve the untouched initial stack pointer
    mov %rsp, %rdi                 # arg1 = initial stack pointer (points to argc)
    lea _DYNAMIC(%rip), %rsi       # arg2 = run-time address of our own _DYNAMIC
    and $-16, %rsp                 # align the stack for the call (System V ABI)
    call _dl_main                  # returns the main image entry point in rax
    mov %r15, %rsp                 # restore the original initial stack pointer
    xor %rdx, %rdx                 # rdx = 0: no shared-object termination hook
    jmp *%rax                      # transfer to the main image real entry
