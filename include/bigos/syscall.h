#ifndef _BIG_SYSCALL_H
#define _BIG_SYSCALL_H

#include <bigos/types.h>
#include <bigos/errno.h>

namespace bigos::irq {
    struct InterruptFrame;
}   // namespace bigos::irq

namespace bigos::sys {
    // Minimal syscall ABI (int 0x80 software-interrupt entry; ring0 self-tests and
    // the default-off first user program use the same register ABI). The mapping below is fixed by source-level checks
    // and documented in docs/en/arch/syscall-entry.md:
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
        SYS_WAIT = 4,          // pid or WAIT_ANY -> child pid, or deterministic negative wait error
        SYS_OPEN = 5,          // user path, read-only flags -> process-local fd
        SYS_READ = 6,          // fd, user buffer, bounded length -> deterministic read result
        SYS_CLOSE = 7,         // fd -> close current process descriptor
        SYS_BRK = 8,           // requested break -> committed heap break or deterministic negative error
        SYS_MAP_ANON = 9,      // length, permissions, flags -> restricted anonymous user mapping
    };

    // POSIX-style error codes live in bigos/errno.h (single source of truth);
    // the dispatcher writes their negated value into the return register, e.g.
    // -bigos::ENOSYS for an unknown syscall number.
    constexpr uint64_t SYS_WRITE_MAX_LEN = 128;
    constexpr uint64_t SYS_IO_MAX_LEN = 512;
    constexpr uint64_t SYS_PATH_MAX_LEN = 256;

    // Syscall dispatch entry. Invoked from irq_dispatch when the interrupt vector
    // is VECTOR_SYSCALL. Reads the syscall number from frame->rax, routes to the
    // matching kernel implementation, and writes the result (or -bigos::ENOSYS
    // for an unknown number) back into frame->rax. It MUST NOT send an i8259 EOI
    // (syscall is not an external IRQ). fd/VFS syscalls additionally check the
    // scheduler blocking guard before allocating or entering synchronous storage IO.
    void dispatch(bigos::irq::InterruptFrame *__frame) noexcept;
}   // namespace bigos::sys

#endif   // _BIG_SYSCALL_H
