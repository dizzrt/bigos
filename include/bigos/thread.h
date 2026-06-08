#ifndef _BIG_THREAD_H
#define _BIG_THREAD_H

#include <bigos/types.h>

NAMESPACE_BIGOS_BEG
namespace sched {
    // Stable single-core kernel thread identity. Not a process id, not a PID
    // namespace, and not a user-visible handle.
    using ThreadId = uint32_t;

    // Kernel thread entry function. Runs in ring0 on its own kernel stack with
    // maskable interrupts enabled. There is no user-mode context, no syscall
    // boundary, and no address-space switch in stage 4.
    using ThreadEntry = void (*)(void *__arg);

    // Bounded early-kernel thread lifecycle state. Blocked/Sleeping are
    // single-core cooperative kernel wait states only: they do not imply POSIX
    // blocking IO, process ownership, user wait queues, or SMP migration.
    enum class ThreadState : uint32_t {
        Runnable = 0,   // on the run queue, waiting to be scheduled
        Running = 1,     // currently executing on the single core
        Idle = 2,        // scheduler-owned idle thread (halts when no work)
        Blocked = 3,     // waiting on an explicit wait queue, not runnable
        Sleeping = 4,    // waiting for a tick deadline or timeout, not runnable
        Terminated = 5,  // exited; retained for deferred reclamation
    };
}   // namespace sched
NAMESPACE_BIGOS_END

#endif   // _BIG_THREAD_H
