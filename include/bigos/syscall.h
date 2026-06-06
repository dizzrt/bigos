#ifndef _BIG_SYSCALL_H
#define _BIG_SYSCALL_H

#include <bigos/types.h>

namespace bigos::irq {
    struct InterruptFrame;
}   // namespace bigos::irq

namespace bigos::sys {
    // Minimal syscall ABI (int 0x80 software-interrupt entry; ring0 self-tests and
    // the default-off first user program use the same register ABI). The mapping below is fixed by source-level checks and documented in
    // docs/arch/syscall-entry.md:
    //
    //   syscall number -> rax           (InterruptFrame.rax on entry)
    //   argument 0     -> rdi           (InterruptFrame.rdi)
    //   argument 1     -> rsi           (InterruptFrame.rsi)
    //   argument 2     -> rdx           (InterruptFrame.rdx)
    //   argument 3     -> r10           (InterruptFrame.r10)
    //   argument 4     -> r8            (InterruptFrame.r8)
    //   argument 5     -> r9            (InterruptFrame.r9)
    //   return value   -> rax           (dispatcher writes InterruptFrame.rax;
    //                                    the caller reads rax after iretq)
    //
    // Registers other than the return value are callee-clobbered by convention;
    // the caller is responsible for saving anything it needs. r10 (not rcx) is the
    // 4th argument register to avoid clashing with rcx, which int 0x80 / iretq
    // semantics make unsuitable as a stable argument slot.

    enum SyscallNumber : uint64_t {
        SYS_DEBUG_WRITE = 0,   // emit a kernel-internal bounded buffer to console/serial
        SYS_GET_TICK = 1,      // return the monotonic kernel tick via rax
        SYS_WRITE = 2,         // fd, user buffer, bounded length -> deterministic write result
        SYS_EXIT = 3,          // exit code -> terminate current user process, does not return
    };

    // Deterministic error code for unknown syscall numbers or invalid requests.
    // Equivalent to -ENOSYS written into the return-value register as a 64-bit
    // two's-complement negative value.
    constexpr int64_t SYS_ENOSYS = -38;
    constexpr int64_t SYS_EFAULT = -14;
    constexpr uint64_t SYS_WRITE_MAX_LEN = 128;

    // Syscall dispatch entry. Invoked from irq_dispatch when the interrupt vector
    // is VECTOR_SYSCALL. Reads the syscall number from frame->rax, routes to the
    // matching kernel implementation, and writes the result (or SYS_ENOSYS for an
    // unknown number) back into frame->rax. It runs in int 0x80 interrupt context:
    // it performs only bounded output/reads and must not allocate or call
    // non-IRQ-safe allocators. It MUST NOT send an i8259 EOI (syscall is not an
    // external IRQ).
    void dispatch(bigos::irq::InterruptFrame *__frame) noexcept;
}   // namespace bigos::sys

#endif   // _BIG_SYSCALL_H
