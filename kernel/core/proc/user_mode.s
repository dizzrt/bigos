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

# bigos_x86_iret_to_user_frame restores a full saved InterruptFrame (see
# include/irq/interrupt.h) and returns to ring3 through iretq. Used by the
# forked child's first scheduling: %rdi points at the saved frame whose layout
# matches isr_common's push order. All general-purpose registers are restored
# from the frame (so the child resumes with the parent's register state plus the
# fork-rewritten rax), and the iretq frame is rebuilt from the saved
# ss/rsp/rflags/cs/rip slots.
.globl bigos_x86_iret_to_user_frame
bigos_x86_iret_to_user_frame:
    # User data segment selector (USER_DATA_SELECTOR = 0x23). %cx is reloaded
    # from the frame below, so using it as a scratch here is safe.
    movw $0x23, %cx
    movw %cx, %ds
    movw %cx, %es

    movq %rdi, %rax            # frame base; %rax is loaded from the frame last

    pushq 128(%rax)           # ss
    pushq 120(%rax)           # rsp
    pushq 168(%rax)           # rflags
    pushq 160(%rax)           # cs
    pushq 152(%rax)           # rip

    movq 0(%rax), %r15
    movq 8(%rax), %r14
    movq 16(%rax), %r13
    movq 24(%rax), %r12
    movq 32(%rax), %r11
    movq 40(%rax), %r10
    movq 48(%rax), %r9
    movq 56(%rax), %r8
    movq 64(%rax), %rdi
    movq 72(%rax), %rsi
    movq 80(%rax), %rbp
    movq 88(%rax), %rdx
    movq 96(%rax), %rcx
    movq 104(%rax), %rbx
    movq 112(%rax), %rax
    iretq
