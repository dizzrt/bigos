.file "switch.s"
.code64

# switch_context(uint64_t *old_sp, uint64_t new_sp)
#
# Cooperative single-core kernel context switch. System V AMD64:
#   rdi = old_sp -> address where the outgoing thread's stack pointer is stored
#   rsi = new_sp -> the incoming thread's saved stack pointer
#
# It saves the callee-saved register set required by the System V AMD64 calling
# convention (rbp, rbx, r12-r15) plus the stack pointer, then loads the incoming
# thread's stack and returns into its saved return address. This does NOT touch
# the InterruptFrame layout, the generated ISR entry frame, or the interrupt
# return path: it is only used by cooperative yield/exit in non-interrupt
# context.
#
# Callers MUST invoke this with maskable interrupts disabled; every resume point
# re-enables interrupts after the switch returns.
.text
.globl switch_context
switch_context:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)
    movq %rsi, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
