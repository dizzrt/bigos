#include <bigos/sched.h>

#include <bigos/arch_context.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/percpu.h>
#include <bigos/smp_ipi.h>
#ifdef BIGOS_USER_PROCESS
#include <bigos/proc.h>
#endif
#include <drivers/irqchip/lapic.h>
#include <irq/interrupt.h>

// Internal allocator flag: alloc_kernel_pages() only returns mapped, accessible
// backing when pre-paging is requested. A kernel thread stack must be touchable
// immediately, so the scheduler requests pre-paged kernel pages here.
#include "../../mm/memdef.h"

NAMESPACE_BIGOS_BEG
namespace sched {

    namespace __detail {
        // The kernel thread scheduler keeps the default normal-thread stack at
        // one page. No smoke/debug build switch changes this page count.
        constexpr uint32_t KERNEL_THREAD_STACK_PAGES = 1;
        constexpr uint32_t DEFAULT_TIME_SLICE_TICKS = 2;
        constexpr int32_t DEFAULT_STATIC_PRIORITY = 0;
        constexpr uint32_t DEFAULT_POLICY_SLOT = 0;
        constexpr uint32_t SCHED_DIAG_OP_NONE = 0;
        constexpr uint32_t SCHED_DIAG_OP_RQ_PUSH = 1;
        constexpr uint32_t SCHED_DIAG_OP_RQ_POP = 2;
        constexpr uint32_t SCHED_DIAG_OP_WAKE = 3;
        constexpr uint32_t SCHED_DIAG_OP_NUDGE = 4;
        constexpr uint32_t SCHED_DIAG_OP_TIMER = 5;

        static_assert(KERNEL_THREAD_STACK_PAGES > 0);
        static_assert(DEFAULT_TIME_SLICE_TICKS > 0);

        struct TCB;

        // Per-thread multi-queue registration node used by wait_queue_wait_any().
        // Each waiting thread owns POLL_MAX_WAIT_QUEUES of these in its TCB, so a
        // thread can register on that many wait queues at once without any
        // allocation on the sleep/wakeup path. A node is linked into at most one
        // queue's poll_head chain at a time (node i tracks queues[i]); q_next
        // threads that per-queue chain and is manipulated only under the owning
        // queue's lock.
        struct PollWaitNode {
            WaitQueue *queue;
            TCB *owner;
            PollWaitNode *q_next;
        };

        struct TCB {
            ThreadId id;
            ThreadState state;
            // Saved stack pointer (the cooperative context pointer). Ownership of
            // this value belongs to the scheduler while the thread is not running.
            uint64_t saved_sp;
            // Kernel stack allocation range recorded for this thread.
            void *stack_base;
            uint32_t stack_pages;
            ThreadEntry entry;
            void *arg;
            // Intrusive run-queue node; lifetime owned by this TCB so the run
            // queue never allocates in IRQ context.
            TCB *rq_next;
            bool on_run_queue;
            cpu::CpuId owner_cpu;
            // Intrusive wait/sleep nodes. A thread may belong to at most one
            // explicit wait queue and one timeout tracking list at a time.
            TCB *wait_next;
            WaitQueue *wait_queue;
            TCB *sleep_next;
            timer::tick_t deadline_tick;
            int wait_result;
            // Bounded timer-driven scheduler metadata. Priority/policy are reserved
            // hooks; default selection remains single-core round-robin.
            uint32_t time_slice_remaining;
            int32_t static_priority;
            uint32_t policy_slot;
            // Intrusive terminated-list node; deferred reclamation only.
            TCB *term_next;
            // The user process (bigos::proc::Process*) this kernel thread runs in
            // ring3, or nullptr for pure kernel threads (idle, smoke helpers, the
            // boot thread). The proc layer's ring3 context (current process, user
            // CR3, TSS rsp0) is global, so when a thread is switched back in the
            // scheduler must re-establish it from this field; otherwise a thread
            // that ran in between (e.g. a forked child that exited) leaves the
            // wrong process / address space active. Untyped here to avoid a
            // scheduler dependency on the proc layout.
            void *user_process;
            // Multi-queue (poll) registration state. poll_registered is the
            // number of poll_nodes currently linked into wait-queue poll_head
            // chains (0 unless this thread is blocked inside
            // wait_queue_wait_any). The array is stable per-thread storage so the
            // sleep/wakeup path never allocates.
            PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES];
            uint32_t poll_registered;
        };

        struct SchedulerSpinLock {
            volatile uint32_t value;
        };

        struct SchedulerCpuState {
            cpu::CpuId cpu_id;
            SchedulerSpinLock lock;
            TCB *current;
            TCB *idle;
            TCB *run_head;
            TCB *run_tail;
            TCB *sleep_head;
            TCB *terminated_head;
            volatile uint32_t run_queue_depth;
            bool initialized;
            bool scheduler_started;
            volatile uint32_t nonblocking_depth;
            volatile uint32_t scheduler_critical_depth;
            volatile uint32_t preemption_disable_depth;
            volatile uint64_t reschedule_intent;
            volatile bool reschedule_pending;
            volatile uint64_t slice_expired_events;
            volatile uint64_t deferred_preemption_events;
            volatile uint64_t irq_return_preemptions;
            volatile cpu::CpuId diag_current_cpu;
            volatile cpu::CpuId diag_target_cpu;
            volatile uint32_t diag_thread_state;
            volatile bool diag_thread_on_run_queue;
            volatile uint32_t diag_operation;
        };

        SchedulerCpuState g_domains[cpu::MAX_CPUS];
        TCB g_idle_tcbs[cpu::MAX_CPUS];
        volatile ThreadId g_next_id = INVALID_THREAD_ID + 1;

        void spin_lock(SchedulerSpinLock *__lock) noexcept {
            while (__atomic_exchange_n(&__lock->value, 1u, __ATOMIC_ACQUIRE) != 0u) {
                while (__atomic_load_n(&__lock->value, __ATOMIC_RELAXED) != 0u)
                    asm volatile("pause" ::: "memory");
            }
        }

        void spin_unlock(SchedulerSpinLock *__lock) noexcept {
            __atomic_store_n(&__lock->value, 0u, __ATOMIC_RELEASE);
        }

        struct SpinGuard {
            SchedulerSpinLock *lock;
            explicit SpinGuard(SchedulerSpinLock *__lock) noexcept : lock(__lock) {
                spin_lock(lock);
            }
            SpinGuard(const SpinGuard &) = delete;
            SpinGuard &operator=(const SpinGuard &) = delete;
            ~SpinGuard() noexcept {
                spin_unlock(lock);
            }
        };

        uint64_t read_rflags() noexcept {
            uint64_t flags;
            asm volatile("pushfq; popq %0" : "=r"(flags)::"memory");
            return flags;
        }

        bool interrupts_enabled() noexcept {
            constexpr uint64_t RFLAGS_IF = 1ull << 9;
            return (read_rflags() & RFLAGS_IF) != 0;
        }

        SchedulerCpuState *domain_for_cpu(cpu::CpuId __id) noexcept {
            if (__id >= cpu::MAX_CPUS)
                return nullptr;
            return &g_domains[__id];
        }

        SchedulerCpuState *current_domain() noexcept {
            SchedulerCpuState *domain = domain_for_cpu(cpu::current_cpu_id());
            if (domain == nullptr || !domain->initialized)
                domain = domain_for_cpu(cpu::BOOTSTRAP_CPU_ID);
            return domain;
        }

        SchedulerCpuState *current_domain_strict() noexcept {
            SchedulerCpuState *domain = domain_for_cpu(cpu::current_cpu_id());
            if (domain == nullptr || !domain->initialized)
                return nullptr;
            return domain;
        }

        bool init_domain(cpu::CpuId __id) noexcept {
            if (!cpu::cpu_id_supported(__id))
                return false;
            SchedulerCpuState *domain = domain_for_cpu(__id);
            if (domain == nullptr)
                return false;

            SpinGuard guard(&domain->lock);
            if (domain->initialized)
                return true;

            domain->cpu_id = __id;
            domain->current = nullptr;
            domain->idle = nullptr;
            domain->run_head = nullptr;
            domain->run_tail = nullptr;
            domain->sleep_head = nullptr;
            domain->terminated_head = nullptr;
            domain->run_queue_depth = 0;
            domain->scheduler_started = false;
            domain->nonblocking_depth = 0;
            domain->scheduler_critical_depth = 0;
            domain->preemption_disable_depth = 0;
            domain->reschedule_intent = 0;
            domain->reschedule_pending = false;
            domain->slice_expired_events = 0;
            domain->deferred_preemption_events = 0;
            domain->irq_return_preemptions = 0;
            domain->diag_current_cpu = cpu::MAX_CPUS;
            domain->diag_target_cpu = cpu::MAX_CPUS;
            domain->diag_thread_state = 0;
            domain->diag_thread_on_run_queue = false;
            domain->diag_operation = SCHED_DIAG_OP_NONE;
            domain->initialized = true;
            return true;
        }

        bool ensure_current_domain() noexcept {
            return init_domain(cpu::current_cpu_id());
        }

        bool cpu_schedulable(cpu::CpuId __id) noexcept {
            SchedulerCpuState *domain = domain_for_cpu(__id);
            return domain != nullptr && domain->initialized && cpu::cpu_online(__id) &&
                   cpu::slot_for(__id).timer_state == cpu::LocalTimerState::Ready;
        }

        void record_scheduler_diag(
            SchedulerCpuState *__domain, TCB *__t, cpu::CpuId __target_cpu, uint32_t __operation) noexcept {
            if (__domain == nullptr)
                return;
            __domain->diag_current_cpu = cpu::current_cpu_id();
            __domain->diag_target_cpu = __target_cpu;
            __domain->diag_thread_state = __t != nullptr ? (uint32_t)__t->state : 0xffffffffu;
            __domain->diag_thread_on_run_queue = __t != nullptr && __t->on_run_queue;
            __domain->diag_operation = __operation;
        }

        ThreadId allocate_thread_id() noexcept {
            return __atomic_fetch_add(&g_next_id, 1u, __ATOMIC_RELAXED);
        }

        void enter_scheduler_critical() noexcept {
            SchedulerCpuState *domain = current_domain();
            ++domain->preemption_disable_depth;
            ++domain->scheduler_critical_depth;
            bigos::cpu::set_preemption_disable_depth(domain->preemption_disable_depth);
        }

        void leave_scheduler_critical() noexcept {
            SchedulerCpuState *domain = current_domain();
            if (domain->scheduler_critical_depth > 0)
                --domain->scheduler_critical_depth;
            if (domain->preemption_disable_depth > 0)
                --domain->preemption_disable_depth;
            bigos::cpu::set_preemption_disable_depth(domain->preemption_disable_depth);
        }

        void request_reschedule_locked(SchedulerCpuState *__domain) noexcept {
            ++__domain->reschedule_intent;
            __domain->reschedule_pending = true;
            cpu::state_for(__domain->cpu_id).reschedule_pending = true;
            if (__domain->preemption_disable_depth > 0)
                ++__domain->deferred_preemption_events;
        }

        void set_current(SchedulerCpuState *__domain, TCB *__t) noexcept {
            __domain->current = __t;
            cpu::LocalState &local = cpu::state_for(__domain->cpu_id);
            local.current_thread = __t;
        }

        void set_reschedule_pending(SchedulerCpuState *__domain, bool __pending) noexcept {
            __domain->reschedule_pending = __pending;
            cpu::state_for(__domain->cpu_id).reschedule_pending = __pending;
        }

        bool has_runnable_peer(SchedulerCpuState *__domain) noexcept {
            TCB *cur = __domain->run_head;
            while (cur != nullptr) {
                if (cur->state == ThreadState::Runnable)
                    return true;
                cur = cur->rq_next;
            }
            return false;
        }

        bool is_ordinary_running_thread(TCB *__t) noexcept {
            SchedulerCpuState *domain = current_domain();
            return __t != nullptr && __t != domain->idle && __t->state == ThreadState::Running;
        }

        void refresh_time_slice(TCB *__t) noexcept {
            SchedulerCpuState *domain = __t != nullptr ? domain_for_cpu(__t->owner_cpu) : nullptr;
            if (__t != nullptr && (domain == nullptr || __t != domain->idle))
                __t->time_slice_remaining = DEFAULT_TIME_SLICE_TICKS;
        }

        // After a switch_context returns into a thread, re-establish the proc
        // layer's global ring3 context (current process, user CR3, TSS rsp0) for
        // the now-current thread. This must run on every resume path because a
        // thread that ran in between may have left a different (or no) user
        // process active. A no-op for kernel threads (user_process == nullptr).
        void restore_user_context_on_resume() noexcept {
#ifdef BIGOS_USER_PROCESS
            SchedulerCpuState *domain = current_domain();
            if (domain->current != nullptr && domain->current->user_process != nullptr)
                bigos::proc::restore_current_user_context((bigos::proc::Process *)domain->current->user_process);
#endif
        }

        void prepare_address_space_for_next(TCB *__next) noexcept {
#ifdef BIGOS_USER_PROCESS
            bigos::proc::prepare_context_switch_to(
                __next != nullptr ? (bigos::proc::Process *)__next->user_process : nullptr);
#else
            (void)__next;
#endif
        }

        void prepare_address_space_before_switch(TCB *__next) noexcept {
#ifdef BIGOS_USER_PROCESS
            // Do not activate a target user CR3 while still running on the
            // outgoing thread's stack. Some outgoing stacks (notably idle's boot
            // stack) are not mapped in the target user root. The incoming user
            // thread restores its CR3/TSS after switch_context resumes on its own
            // kernel stack.
            if (__next != nullptr && __next->user_process != nullptr)
                return;
#endif
            prepare_address_space_for_next(__next);
        }

        bool can_preempt_from_irq_return(const irq::InterruptFrame *__frame) noexcept {
            SchedulerCpuState *domain = current_domain();
            return domain != nullptr && arch_context::is_kernel_irq_return_context(__frame) && domain->scheduler_started &&
                   is_ordinary_running_thread(domain->current) && domain->reschedule_pending &&
                   domain->preemption_disable_depth == 0 && domain->scheduler_critical_depth == 0 &&
                   domain->nonblocking_depth <= 1 && has_runnable_peer(domain);
        }

        void rq_push(SchedulerCpuState *__domain, TCB *__t) noexcept {
            if (__domain == nullptr || __t == nullptr || __t->state != ThreadState::Runnable || __t->on_run_queue) {
                record_scheduler_diag(__domain, __t, __domain != nullptr ? __domain->cpu_id : cpu::MAX_CPUS,
                    SCHED_DIAG_OP_RQ_PUSH);
                return;
            }
            __t->rq_next = nullptr;
            __t->owner_cpu = __domain->cpu_id;
            __t->on_run_queue = true;
            if (__domain->run_tail == nullptr) {
                __domain->run_head = __t;
                __domain->run_tail = __t;
            } else {
                __domain->run_tail->rq_next = __t;
                __domain->run_tail = __t;
            }
            ++__domain->run_queue_depth;
        }

        TCB *rq_pop(SchedulerCpuState *__domain) noexcept {
            while (__domain != nullptr && __domain->run_head != nullptr) {
                TCB *t = __domain->run_head;
                __domain->run_head = t->rq_next;
                if (__domain->run_head == nullptr)
                    __domain->run_tail = nullptr;
                t->rq_next = nullptr;
                t->on_run_queue = false;
                if (__domain->run_queue_depth > 0)
                    --__domain->run_queue_depth;
                if (t->state == ThreadState::Runnable) {
                    record_scheduler_diag(__domain, t, __domain->cpu_id, SCHED_DIAG_OP_RQ_POP);
                    return t;
                }
            }
            return nullptr;
        }

        void wait_queue_lock(WaitQueue *__queue) noexcept {
            if (__queue != nullptr)
                spin_lock((SchedulerSpinLock *)&__queue->lock);
        }

        void wait_queue_unlock(WaitQueue *__queue) noexcept {
            if (__queue != nullptr)
                spin_unlock((SchedulerSpinLock *)&__queue->lock);
        }

        void wait_queue_push_locked(WaitQueue *__queue, TCB *__t) noexcept {
            __t->wait_next = nullptr;
            __t->wait_queue = __queue;
            if (__queue->tail == nullptr) {
                __queue->head = __t;
                __queue->tail = __t;
            } else {
                ((TCB *)__queue->tail)->wait_next = __t;
                __queue->tail = __t;
            }
        }

        void wait_queue_remove_locked(TCB *__t) noexcept {
            WaitQueue *queue = __t->wait_queue;
            if (queue == nullptr)
                return;

            TCB *prev = nullptr;
            TCB *cur = (TCB *)queue->head;
            while (cur != nullptr) {
                if (cur == __t) {
                    if (prev == nullptr)
                        queue->head = cur->wait_next;
                    else
                        prev->wait_next = cur->wait_next;
                    if (queue->tail == cur)
                        queue->tail = prev;
                    cur->wait_next = nullptr;
                    cur->wait_queue = nullptr;
                    return;
                }
                prev = cur;
                cur = cur->wait_next;
            }
            __t->wait_queue = nullptr;
            __t->wait_next = nullptr;
        }

        // Push a poll node onto a queue's poll_head chain. Caller holds the
        // queue's lock. The node's queue pointer is set by the registration path.
        void poll_queue_push_locked(WaitQueue *__queue, PollWaitNode *__node) noexcept {
            __node->q_next = (PollWaitNode *)__queue->poll_head;
            __queue->poll_head = __node;
        }

        // Remove a poll node from its queue's poll_head chain if still linked.
        // Caller holds the queue's lock. Idempotent: a node already unlinked by a
        // producer wake path (its queue slot cleared) is skipped.
        void poll_queue_remove_locked(WaitQueue *__queue, PollWaitNode *__node) noexcept {
            PollWaitNode *prev = nullptr;
            PollWaitNode *cur = (PollWaitNode *)__queue->poll_head;
            while (cur != nullptr) {
                if (cur == __node) {
                    if (prev == nullptr)
                        __queue->poll_head = cur->q_next;
                    else
                        prev->q_next = cur->q_next;
                    cur->q_next = nullptr;
                    return;
                }
                prev = cur;
                cur = cur->q_next;
            }
        }

        void sleep_push_locked(SchedulerCpuState *__domain, TCB *__t) noexcept {
            __t->sleep_next = __domain->sleep_head;
            __domain->sleep_head = __t;
        }

        void sleep_remove_locked(SchedulerCpuState *__domain, TCB *__t) noexcept {
            TCB *prev = nullptr;
            TCB *cur = __domain->sleep_head;
            while (cur != nullptr) {
                if (cur == __t) {
                    if (prev == nullptr)
                        __domain->sleep_head = cur->sleep_next;
                    else
                        prev->sleep_next = cur->sleep_next;
                    cur->sleep_next = nullptr;
                    return;
                }
                prev = cur;
                cur = cur->sleep_next;
            }
            __t->sleep_next = nullptr;
        }

        bool wake_thread_locked(SchedulerCpuState *__domain, TCB *__t, int __result) noexcept {
            if (__t == nullptr || __t->state == ThreadState::Terminated || __t->state == ThreadState::Runnable ||
                __t->state == ThreadState::Running || __t->state == ThreadState::Idle)
                return false;

            wait_queue_remove_locked(__t);
            sleep_remove_locked(__domain, __t);
            __t->wait_result = __result;
            __t->state = ThreadState::Runnable;
            refresh_time_slice(__t);
            rq_push(__domain, __t);
            request_reschedule_locked(__domain);
            record_scheduler_diag(__domain, __t, __domain->cpu_id, SCHED_DIAG_OP_WAKE);
            return true;
        }

        void schedule_blocked_current_locked(SchedulerCpuState *__domain, TCB *__prev) noexcept {
            TCB *next = rq_pop(__domain);
            if (next == nullptr)
                next = __domain->idle;
            else {
                next->state = ThreadState::Running;
                refresh_time_slice(next);
            }

            set_current(__domain, next);
            set_reschedule_pending(__domain, false);
            spin_unlock(&__domain->lock);
            prepare_address_space_before_switch(next);
            arch_context::switch_kernel_context(&__prev->saved_sp, next->saved_sp);
            restore_user_context_on_resume();
        }

        void nudge_cpu(cpu::CpuId __target_cpu) noexcept {
            if (__target_cpu == cpu::current_cpu_id() || !cpu_schedulable(__target_cpu))
                return;
            const smp::IpiDeliveryResult result = smp::send_ipi(__target_cpu, smp::IpiType::SchedulerNudge);
            if (result.status != smp::IpiDeliveryStatus::Delivered) {
                SchedulerCpuState *domain = domain_for_cpu(__target_cpu);
                record_scheduler_diag(domain, nullptr, __target_cpu, SCHED_DIAG_OP_NUDGE);
            }
        }

        // Drain the poll_head chain of a queue and make each registered owner
        // runnable. Called by wake_one()/wake_all() (producers, possibly in IRQ
        // context). The queue lock MUST NOT be held on entry; this helper acquires
        // it only to unlink one node at a time, then takes the owner's domain lock
        // separately (never both at once, matching the single-waiter wake
        // discipline). It only removes each node from THIS queue's poll_head; the
        // woken thread self-clears its remaining registrations on its resume path.
        // wake_thread_locked() is idempotent, so a thread already made runnable via
        // another queue is simply skipped here.
        void drain_poll_head(WaitQueue *__queue) noexcept {
            for (;;) {
                wait_queue_lock(__queue);
                PollWaitNode *node = (PollWaitNode *)__queue->poll_head;
                if (node == nullptr) {
                    wait_queue_unlock(__queue);
                    return;
                }
                // Unlink this node from the queue before dropping the lock so a
                // concurrent producer never re-processes it.
                __queue->poll_head = node->q_next;
                node->q_next = nullptr;
                TCB *owner = node->owner;
                // Clear the node's queue tag so the owner's self-cleanup skips this
                // already-unlinked slot.
                node->queue = nullptr;
                wait_queue_unlock(__queue);

                if (owner == nullptr)
                    continue;
                SchedulerCpuState *domain = domain_for_cpu(owner->owner_cpu);
                if (domain == nullptr)
                    continue;
                spin_lock(&domain->lock);
                const bool woke = wake_thread_locked(domain, owner, WAIT_OK);
                const cpu::CpuId target_cpu = domain->cpu_id;
                spin_unlock(&domain->lock);
                if (woke)
                    nudge_cpu(target_cpu);
            }
        }

        // New-thread startup path. Entered for the first time through the prepared
        // stack frame after switch_context returns into it. Interrupts are still
        // masked from the switching thread, so enable them before running entry.
        extern "C" void thread_trampoline() noexcept {
            bigos::irq::enableIRQ();
            TCB *self = current_domain()->current;
            self->entry(self->arg);
            sched::thread_exit();
        }
    }   // namespace __detail

    namespace __detail {
        cpu::CpuId select_placement_cpu() noexcept {
            (void)ensure_current_domain();
            const cpu::CpuId current = cpu::current_cpu_id();
            cpu::CpuId best = cpu_schedulable(current) ? current : cpu::BOOTSTRAP_CPU_ID;
            uint32_t best_depth = 0xffffffffu;

            SchedulerCpuState *current_domain = domain_for_cpu(current);
            if (current_domain != nullptr && current_domain->initialized) {
                const uint32_t local_depth = __atomic_load_n(&current_domain->run_queue_depth, __ATOMIC_RELAXED);
                if (cpu_schedulable(current) && local_depth <= 3)
                    return current;
                best_depth = local_depth;
            }

            for (cpu::CpuId id = 0; id < cpu::MAX_CPUS; id++) {
                SchedulerCpuState *domain = domain_for_cpu(id);
                if (domain == nullptr || !cpu_schedulable(id))
                    continue;
                const uint32_t depth = __atomic_load_n(&domain->run_queue_depth, __ATOMIC_RELAXED);
                if (depth < best_depth || (depth == best_depth && id < best)) {
                    best = id;
                    best_depth = depth;
                }
            }
            return best;
        }

        ThreadId create_kernel_thread_target(ThreadEntry __entry, void *__arg, cpu::CpuId __target_cpu) noexcept {
            if (__entry == nullptr)
                return INVALID_THREAD_ID;
            if (!init_domain(cpu::BOOTSTRAP_CPU_ID) || !cpu_schedulable(__target_cpu))
                return INVALID_THREAD_ID;

            // Non-interrupt context only: ordinary allocator under the phase 3
            // contract. Never reached from any IRQ handler.
            TCB *tcb = (TCB *)kmalloc(sizeof(TCB));
            if (tcb == nullptr)
                return INVALID_THREAD_ID;

            void *stack_base = alloc_kernel_pages(KERNEL_THREAD_STACK_PAGES, _GFM_PRE_PAGING);
            if (stack_base == nullptr) {
                // Failure path must not violate the phase 3 allocator contract:
                // free only what this path allocated, in non-interrupt context.
                bigos::free(tcb);
                return INVALID_THREAD_ID;
            }

            tcb->id = allocate_thread_id();
            tcb->state = ThreadState::Runnable;
            tcb->saved_sp = 0;
            tcb->stack_base = stack_base;
            tcb->stack_pages = KERNEL_THREAD_STACK_PAGES;
            tcb->entry = __entry;
            tcb->arg = __arg;
            tcb->rq_next = nullptr;
            tcb->on_run_queue = false;
            tcb->owner_cpu = __target_cpu;
            tcb->wait_next = nullptr;
            tcb->wait_queue = nullptr;
            tcb->sleep_next = nullptr;
            tcb->deadline_tick = 0;
            tcb->wait_result = WAIT_OK;
            tcb->time_slice_remaining = DEFAULT_TIME_SLICE_TICKS;
            tcb->static_priority = DEFAULT_STATIC_PRIORITY;
            tcb->policy_slot = DEFAULT_POLICY_SLOT;
            tcb->term_next = nullptr;
            tcb->user_process = nullptr;
            tcb->poll_registered = 0;

            // Build the initial stack frame so the first switch_context resume lands
            // in thread_trampoline with a 16-byte-aligned System V frame. Layout from
            // saved_sp upward: r15, r14, r13, r12, rbx, rbp, return-address, pad.
            const uint64_t stack_top = (uint64_t)stack_base + (uint64_t)KERNEL_THREAD_STACK_PAGES * PAGE_SIZE;
            uint64_t *sp = (uint64_t *)stack_top;
            sp -= 8;
            sp[0] = 0;                                      // r15
            sp[1] = 0;                                      // r14
            sp[2] = 0;                                      // r13
            sp[3] = 0;                                      // r12
            sp[4] = 0;                                      // rbx
            sp[5] = 0;                                      // rbp
            sp[6] = (uint64_t)&thread_trampoline;           // return address
            sp[7] = 0;                                      // alignment padding
            tcb->saved_sp = (uint64_t)sp;

            SchedulerCpuState *target = domain_for_cpu(__target_cpu);
            bigos::irq::InterruptGuard irq_guard;
            spin_lock(&target->lock);
            rq_push(target, tcb);
            request_reschedule_locked(target);
            spin_unlock(&target->lock);
            nudge_cpu(__target_cpu);
            return tcb->id;
        }
    }   // namespace __detail

    ThreadId create_kernel_thread(ThreadEntry __entry, void *__arg) noexcept {
        return __detail::create_kernel_thread_target(__entry, __arg, __detail::select_placement_cpu());
    }

    ThreadId create_kernel_thread_on_cpu(ThreadEntry __entry, void *__arg, cpu::CpuId __target_cpu) noexcept {
        return __detail::create_kernel_thread_target(__entry, __arg, __target_cpu);
    }

    void yield() noexcept {
        bigos::irq::disableIRQ();
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();

        __detail::TCB *prev = domain->current;
        __detail::TCB *next = __detail::rq_pop(domain);

        if (next == nullptr) {
            __detail::leave_scheduler_critical();
            __detail::spin_unlock(&domain->lock);
            bigos::irq::enableIRQ();
            return;
        }

        if (prev != nullptr && prev->state == ThreadState::Running) {
            prev->state = ThreadState::Runnable;
            __detail::rq_push(domain, prev);
        }

        next->state = ThreadState::Running;
        __detail::refresh_time_slice(next);
        __detail::set_reschedule_pending(domain, false);
        __detail::set_current(domain, next);
        __detail::leave_scheduler_critical();
        __detail::spin_unlock(&domain->lock);
        __detail::prepare_address_space_before_switch(next);
        arch_context::switch_kernel_context(&prev->saved_sp, next->saved_sp);

        __detail::restore_user_context_on_resume();
        bigos::irq::enableIRQ();
    }

    void set_current_user_process(void *__process) noexcept {
        bigos::irq::InterruptGuard guard;
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::SpinGuard lock(&domain->lock);
        if (domain->current != nullptr)
            domain->current->user_process = __process;
    }

    void thread_exit() noexcept {
        bigos::irq::disableIRQ();
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();
        __detail::TCB *prev = domain->current;
        if (prev->wait_queue != nullptr) {
            WaitQueue *queue = prev->wait_queue;
            __detail::wait_queue_lock(queue);
            __detail::wait_queue_remove_locked(prev);
            __detail::wait_queue_unlock(queue);
        }
        __detail::sleep_remove_locked(domain, prev);
        prev->state = ThreadState::Terminated;
        // Remove from runnable scheduling and retain on the terminated list.
        // The current TCB and kernel stack are NOT freed on this exit stack;
        // safe reclamation is deferred to a later lifecycle change.
        prev->term_next = domain->terminated_head;
        domain->terminated_head = prev;

        __detail::TCB *next = __detail::rq_pop(domain);
        if (next == nullptr)
            next = domain->idle;
        else {
            next->state = ThreadState::Running;
            __detail::refresh_time_slice(next);
        }

        uint64_t discarded_sp = 0;
        prev->saved_sp = 0;

        __detail::set_current(domain, next);
        __detail::leave_scheduler_critical();
        __detail::spin_unlock(&domain->lock);
        __detail::prepare_address_space_before_switch(next);
        arch_context::switch_kernel_context(&discarded_sp, next->saved_sp);

        for (;;)
            asm volatile("hlt");
    }

    bool init_current_cpu_domain() noexcept {
        return __detail::init_domain(cpu::current_cpu_id());
    }

    void start() noexcept {
        const cpu::CpuId cpu_id = cpu::current_cpu_id();
        (void)__detail::init_domain(cpu_id);
        __detail::SchedulerCpuState *domain = __detail::domain_for_cpu(cpu_id);
        __detail::TCB *idle = &__detail::g_idle_tcbs[cpu_id];

        idle->id = __detail::allocate_thread_id();
        idle->state = ThreadState::Idle;
        idle->saved_sp = 0;
        idle->stack_base = nullptr;
        idle->stack_pages = 0;
        idle->entry = nullptr;
        idle->arg = nullptr;
        idle->rq_next = nullptr;
        idle->on_run_queue = false;
        idle->owner_cpu = cpu_id;
        idle->wait_next = nullptr;
        idle->wait_queue = nullptr;
        idle->sleep_next = nullptr;
        idle->deadline_tick = 0;
        idle->wait_result = WAIT_OK;
        idle->time_slice_remaining = 0;
        idle->static_priority = __detail::DEFAULT_STATIC_PRIORITY;
        idle->policy_slot = __detail::DEFAULT_POLICY_SLOT;
        idle->term_next = nullptr;
        idle->user_process = nullptr;
        idle->poll_registered = 0;

        {
            bigos::irq::InterruptGuard guard;
            __detail::SpinGuard lock(&domain->lock);
            domain->idle = idle;
            __detail::set_current(domain, idle);
            domain->scheduler_started = true;
        }

        // Idle thread owns halt behavior for each CPU: run local work, otherwise
        // hlt until a timer or scheduler nudge wakes this CPU, then re-evaluate.
        for (;;) {
            sched::yield();
#ifdef BIGOS_USER_PROCESS
            if (cpu::is_bootstrap_cpu())
                bigos::proc::reap_pending_processes();
#endif
            asm volatile("hlt");
        }
    }

    bool can_block() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        return domain->scheduler_started && domain->current != nullptr && domain->current != domain->idle &&
               domain->current->state == ThreadState::Running && domain->nonblocking_depth == 0 &&
               domain->scheduler_critical_depth == 0 && __detail::interrupts_enabled();
    }

    bool can_allocate_in_fault() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        return domain->scheduler_started && domain->current != nullptr && domain->current != domain->idle &&
               domain->current->state == ThreadState::Running && domain->scheduler_critical_depth == 0;
    }

    void enter_nonblocking_context() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        ++domain->nonblocking_depth;
        bigos::cpu::set_nonblocking_depth(domain->nonblocking_depth);
    }

    void leave_nonblocking_context() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        if (domain->nonblocking_depth > 0)
            --domain->nonblocking_depth;
        bigos::cpu::set_nonblocking_depth(domain->nonblocking_depth);
    }

    void disable_preemption() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        ++domain->preemption_disable_depth;
        bigos::cpu::set_preemption_disable_depth(domain->preemption_disable_depth);
    }

    void enable_preemption() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        if (domain->preemption_disable_depth > 0)
            --domain->preemption_disable_depth;
        bigos::cpu::set_preemption_disable_depth(domain->preemption_disable_depth);
    }

    bool preemption_enabled() noexcept {
        return __detail::current_domain()->preemption_disable_depth == 0;
    }

    bool reschedule_pending() noexcept {
        return __detail::current_domain()->reschedule_pending;
    }

    void init_wait_queue(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return;
        __queue->head = nullptr;
        __queue->tail = nullptr;
        __queue->lock = 0;
        __queue->poll_head = nullptr;
    }

    bool wait_queue_empty(const WaitQueue *__queue) noexcept {
        return __queue == nullptr || __queue->head == nullptr;
    }

    int wait_queue_wait_until(
        WaitQueue *__queue, WaitPredicate __predicate, void *__arg, timer::tick_t __timeout_ticks) noexcept {
        if (__queue == nullptr)
            return WAIT_INVALID;
        if (!can_block())
            return WAIT_BLOCK_FORBIDDEN;

        bigos::irq::disableIRQ();
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();
        __detail::wait_queue_lock(__queue);

        if (__predicate != nullptr && __predicate(__arg)) {
            __detail::wait_queue_unlock(__queue);
            __detail::leave_scheduler_critical();
            __detail::spin_unlock(&domain->lock);
            bigos::irq::enableIRQ();
            return WAIT_OK;
        }

        __detail::TCB *self = domain->current;
        self->wait_result = WAIT_OK;
        __detail::wait_queue_push_locked(__queue, self);
        if (__timeout_ticks > 0) {
            self->deadline_tick = timer::ticks() + __timeout_ticks;
            self->state = ThreadState::Sleeping;
            __detail::sleep_push_locked(domain, self);
        } else {
            self->deadline_tick = 0;
            self->state = ThreadState::Blocked;
        }

        __detail::leave_scheduler_critical();
        __detail::wait_queue_unlock(__queue);
        __detail::schedule_blocked_current_locked(domain, self);

        // Resumed by wakeup or timeout. The switcher left IRQs masked.
        bigos::irq::enableIRQ();
        return self->wait_result;
    }

    int wait_queue_wait_any(
        WaitQueue **__queues, uint32_t __count, WaitPredicate __predicate, void *__arg,
        timer::tick_t __timeout_ticks) noexcept {
        if (__count > POLL_MAX_WAIT_QUEUES)
            return WAIT_INVALID;
        if (__count > 0 && __queues == nullptr)
            return WAIT_INVALID;
        if (!can_block())
            return WAIT_BLOCK_FORBIDDEN;

        bigos::irq::disableIRQ();
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();

        __detail::TCB *self = domain->current;

        // Register-first: link one poll node per queue before checking the
        // predicate. Because self holds the domain lock across registration, the
        // predicate check, and the block decision, a producer that fires after we
        // register can unlink our node (queue lock) but cannot complete the wake
        // (needs the domain lock) until we release it in schedule_blocked. This
        // closes the check/enqueue race the same way the single-queue path does.
        for (uint32_t i = 0; i < __count; i++) {
            __detail::PollWaitNode *node = &self->poll_nodes[i];
            node->owner = self;
            node->queue = __queues[i];
            node->q_next = nullptr;
            if (__queues[i] != nullptr) {
                __detail::wait_queue_lock(__queues[i]);
                __detail::poll_queue_push_locked(__queues[i], node);
                __detail::wait_queue_unlock(__queues[i]);
            }
        }
        self->poll_registered = __count;

        // Level-triggered readiness: if already satisfied, undo registration and
        // return without blocking.
        if (__predicate != nullptr && __predicate(__arg)) {
            for (uint32_t i = 0; i < __count; i++) {
                __detail::PollWaitNode *node = &self->poll_nodes[i];
                if (__queues[i] != nullptr) {
                    __detail::wait_queue_lock(__queues[i]);
                    __detail::poll_queue_remove_locked(__queues[i], node);
                    __detail::wait_queue_unlock(__queues[i]);
                }
                node->queue = nullptr;
            }
            self->poll_registered = 0;
            __detail::leave_scheduler_critical();
            __detail::spin_unlock(&domain->lock);
            bigos::irq::enableIRQ();
            return WAIT_OK;
        }

        self->wait_result = WAIT_OK;
        if (__timeout_ticks > 0) {
            self->deadline_tick = timer::ticks() + __timeout_ticks;
            self->state = ThreadState::Sleeping;
            __detail::sleep_push_locked(domain, self);
        } else {
            self->deadline_tick = 0;
            self->state = ThreadState::Blocked;
        }

        __detail::leave_scheduler_critical();
        __detail::schedule_blocked_current_locked(domain, self);

        // Resumed by any registered queue's wake or by timeout. Self-clean every
        // poll node from its queue (the waker only removed nodes from the queue it
        // fired on; other registrations remain and must be removed here). The
        // remove is idempotent for an already-unlinked node.
        for (uint32_t i = 0; i < self->poll_registered; i++) {
            __detail::PollWaitNode *node = &self->poll_nodes[i];
            WaitQueue *queue = __queues[i];
            if (queue != nullptr) {
                __detail::wait_queue_lock(queue);
                __detail::poll_queue_remove_locked(queue, node);
                __detail::wait_queue_unlock(queue);
            }
            node->queue = nullptr;
        }
        self->poll_registered = 0;

        bigos::irq::enableIRQ();
        return self->wait_result;
    }

    uint32_t wake_one(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return 0;

        bigos::irq::InterruptGuard guard;
        __detail::wait_queue_lock(__queue);
        __detail::TCB *t = (__detail::TCB *)__queue->head;
        while (t != nullptr && t->state == ThreadState::Terminated) {
            __detail::TCB *next = t->wait_next;
            __detail::wait_queue_remove_locked(t);
            t = next;
        }
        if (t == nullptr) {
            __detail::wait_queue_unlock(__queue);
            // No single-waiter, but multi-queue poll waiters are level-triggered
            // and must still be woken to re-scan their readiness set.
            __detail::drain_poll_head(__queue);
            return 0;
        }
        const bigos::cpu::CpuId owner_cpu = t->owner_cpu;
        __detail::wait_queue_remove_locked(t);
        __detail::wait_queue_unlock(__queue);

        __detail::SchedulerCpuState *domain = __detail::domain_for_cpu(owner_cpu);
        if (domain == nullptr) {
            __detail::drain_poll_head(__queue);
            return 0;
        }
        __detail::spin_lock(&domain->lock);
        const uint32_t woke = __detail::wake_thread_locked(domain, t, WAIT_OK) ? 1u : 0u;
        const cpu::CpuId target_cpu = domain->cpu_id;
        __detail::spin_unlock(&domain->lock);
        if (woke != 0)
            __detail::nudge_cpu(target_cpu);
        // Poll waiters on this queue are level-triggered; wake them all so each
        // re-scans and re-registers if still not ready.
        __detail::drain_poll_head(__queue);
        return woke;
    }

    uint32_t wake_all(WaitQueue *__queue) noexcept {
        if (__queue == nullptr)
            return 0;

        bigos::irq::InterruptGuard guard;
        __detail::wait_queue_lock(__queue);
        uint32_t count = 0;
        while (__queue->head != nullptr) {
            __detail::TCB *t = (__detail::TCB *)__queue->head;
            const bigos::cpu::CpuId owner_cpu = t->owner_cpu;
            __detail::wait_queue_remove_locked(t);
            __detail::wait_queue_unlock(__queue);

            __detail::SchedulerCpuState *domain = __detail::domain_for_cpu(owner_cpu);
            if (domain == nullptr) {
                __detail::wait_queue_lock(__queue);
                continue;
            }
            __detail::spin_lock(&domain->lock);
            const bool woke = __detail::wake_thread_locked(domain, t, WAIT_OK);
            const cpu::CpuId target_cpu = domain->cpu_id;
            __detail::spin_unlock(&domain->lock);
            if (woke) {
                ++count;
                __detail::nudge_cpu(target_cpu);
            }
            __detail::wait_queue_lock(__queue);
        }
        __detail::wait_queue_unlock(__queue);
        // Wake every multi-queue poll waiter registered on this queue too.
        __detail::drain_poll_head(__queue);
        return count;
    }

    int sleep_for(timer::tick_t __ticks) noexcept {
        if (__ticks == 0)
            return WAIT_OK;
        if (!can_block())
            return WAIT_BLOCK_FORBIDDEN;

        bigos::irq::disableIRQ();
        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();

        __detail::TCB *self = domain->current;
        self->wait_result = WAIT_TIMEOUT;
        self->deadline_tick = timer::ticks() + __ticks;
        self->state = ThreadState::Sleeping;
        __detail::sleep_push_locked(domain, self);

        __detail::leave_scheduler_critical();
        __detail::schedule_blocked_current_locked(domain, self);

        bigos::irq::enableIRQ();
        return self->wait_result;
    }

    void maybe_preempt_on_irq_return(irq::InterruptFrame *__frame) noexcept {
        if (!__detail::can_preempt_from_irq_return(__frame))
            return;

        __detail::SchedulerCpuState *domain = __detail::current_domain();
        __detail::spin_lock(&domain->lock);
        __detail::enter_scheduler_critical();
        __detail::TCB *prev = domain->current;
        __detail::TCB *next = __detail::rq_pop(domain);
        if (next == nullptr) {
            __detail::leave_scheduler_critical();
            __detail::spin_unlock(&domain->lock);
            return;
        }

        if (prev->state == ThreadState::Running) {
            prev->state = ThreadState::Runnable;
            __detail::rq_push(domain, prev);
        }

        next->state = ThreadState::Running;
        __detail::refresh_time_slice(next);
        __detail::set_current(domain, next);
        __detail::set_reschedule_pending(domain, false);
        ++domain->irq_return_preemptions;
        __detail::leave_scheduler_critical();
        __detail::spin_unlock(&domain->lock);

        arch_context::switch_kernel_context(&prev->saved_sp, next->saved_sp);
        __detail::restore_user_context_on_resume();
    }

    void on_timer_tick() noexcept {
        // IRQ-context-safe and bounded: no allocation, no IO, no blocking, and
        // no direct thread switch. Context switching is deferred to explicit
        // scheduler-owned return boundaries after EOI.
        const timer::tick_t now = timer::ticks();
        __detail::SchedulerCpuState *domain = __detail::current_domain_strict();
        if (domain == nullptr || !domain->initialized)
            return;

        __detail::spin_lock(&domain->lock);
        __detail::record_scheduler_diag(domain, domain->current, domain->cpu_id, __detail::SCHED_DIAG_OP_TIMER);
        __detail::TCB *prev = nullptr;
        __detail::TCB *cur = domain->sleep_head;
        while (cur != nullptr) {
            __detail::TCB *next = cur->sleep_next;
            if (now >= cur->deadline_tick) {
                if (prev == nullptr)
                    domain->sleep_head = next;
                else
                    prev->sleep_next = next;
                cur->sleep_next = nullptr;
                if (cur->wait_queue != nullptr) {
                    WaitQueue *queue = cur->wait_queue;
                    __detail::wait_queue_lock(queue);
                    __detail::wait_queue_remove_locked(cur);
                    __detail::wait_queue_unlock(queue);
                }
                if (cur->state == ThreadState::Sleeping) {
                    cur->wait_result = WAIT_TIMEOUT;
                    cur->state = ThreadState::Runnable;
                    __detail::rq_push(domain, cur);
                    __detail::request_reschedule_locked(domain);
                }
            } else {
                prev = cur;
            }
            cur = next;
        }

        __detail::TCB *current = domain->current;
        if (!__detail::is_ordinary_running_thread(current)) {
            __detail::spin_unlock(&domain->lock);
            return;
        }

        if (current->time_slice_remaining > 0)
            --current->time_slice_remaining;

        if (current->time_slice_remaining == 0) {
            ++domain->slice_expired_events;
            __detail::request_reschedule_locked(domain);
        }
        __detail::spin_unlock(&domain->lock);
    }

    void on_scheduler_nudge() noexcept {
        __detail::SchedulerCpuState *domain = __detail::current_domain_strict();
        if (domain == nullptr || !domain->initialized)
            return;
        __detail::SpinGuard lock(&domain->lock);
        __detail::request_reschedule_locked(domain);
    }
}   // namespace sched
NAMESPACE_BIGOS_END
