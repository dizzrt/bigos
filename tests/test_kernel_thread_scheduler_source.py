from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_thread_and_sched_headers_declare_bounded_state_and_context_contracts() -> None:
    thread_h = read_source('include/bigos/thread.h')
    sched_h = read_source('include/bigos/sched.h')

    # Bounded early-kernel lifecycle state, no blocking/user/SMP implications.
    for token in ('Runnable', 'Running', 'Idle', 'Terminated'):
        assert token in thread_h
    assert 'enum class ThreadState' in thread_h
    assert 'using ThreadId' in thread_h
    assert 'using ThreadEntry' in thread_h

    # Public API surface.
    assert 'ThreadId create_kernel_thread(ThreadEntry __entry, void *__arg) noexcept;' in sched_h
    assert 'void yield() noexcept;' in sched_h
    assert '[[noreturn]] void thread_exit() noexcept;' in sched_h
    assert 'void start() noexcept;' in sched_h
    assert 'void on_timer_tick() noexcept;' in sched_h

    # Non-interrupt-context and single-core annotations.
    assert 'Non-interrupt-context only' in sched_h
    assert 'Single-core only' in sched_h or 'single-core' in sched_h.lower()
    assert 'IRQ-context-safe' in sched_h


def test_default_kernel_thread_stack_is_one_page_without_build_switch() -> None:
    sched = read_source('src/kernel/sched/sched.cc')
    xmake = read_source('xmake.lua')

    assert 'KERNEL_THREAD_STACK_PAGES = 1' in sched
    assert 'alloc_kernel_pages(__detail::KERNEL_THREAD_STACK_PAGES, _GFM_PRE_PAGING)' in sched
    assert 'stack_base' in sched and 'stack_pages' in sched

    # No build switch changes the default thread stack page count.
    assert 'scheduler_stack_pages' not in xmake


def test_terminated_threads_defer_reclamation() -> None:
    sched = read_source('src/kernel/sched/sched.cc')

    exit_start = sched.index('void thread_exit()')
    exit_end = sched.index('void start()')
    exit_body = sched[exit_start:exit_end]

    assert 'ThreadState::Terminated' in exit_body
    assert 'g_terminated_head' in exit_body
    # The exit path must NOT free the current TCB or stack on the exit stack.
    assert 'free(' not in exit_body
    assert 'free_pages' not in exit_body


def test_create_failure_path_obeys_allocator_contract() -> None:
    sched = read_source('src/kernel/sched/sched.cc')

    create_start = sched.index('ThreadId create_kernel_thread')
    create_end = sched.index('void yield()')
    create_body = sched[create_start:create_end]

    # On stack allocation failure only the TCB this path allocated is freed.
    assert 'bigos::free(tcb);' in create_body
    assert 'return INVALID_THREAD_ID;' in create_body


def test_context_switch_symbol_saves_callee_saved_set() -> None:
    switch_s = read_source('src/kernel/sched/switch.s')
    sched = read_source('src/kernel/sched/sched.cc')

    assert '.globl switch_context' in switch_s
    assert 'switch_context:' in switch_s

    save_order = [
        'pushq %rbp',
        'pushq %rbx',
        'pushq %r12',
        'pushq %r13',
        'pushq %r14',
        'pushq %r15',
    ]
    indices = [switch_s.index(token) for token in save_order]
    assert indices == sorted(indices)

    # Saves old sp and loads new sp, returns into the resumed thread.
    assert 'movq %rsp, (%rdi)' in switch_s
    assert 'movq %rsi, %rsp' in switch_s
    assert 'ret' in switch_s

    # C++ side declares the helper and a scheduler-owned trampoline entry.
    assert 'extern "C" void switch_context(uint64_t *__old_sp, uint64_t __new_sp) noexcept;' in sched
    assert 'thread_trampoline' in sched
    assert 'self->entry(self->arg);' in sched
    assert 'sched::thread_exit();' in sched


def test_cooperative_switch_does_not_touch_interrupt_frame_abi() -> None:
    switch_s = read_source('src/kernel/sched/switch.s')
    interrupt_s = read_source('src/kernel/irq/interrupt.s')

    # The cooperative switch must not redefine the ISR entry/iretq path.
    assert 'iretq' not in switch_s
    assert 'isr_common' not in switch_s
    # The ISR ABI path remains the sole owner of iretq.
    assert 'iretq' in interrupt_s


def test_yield_is_round_robin_single_core() -> None:
    sched = read_source('src/kernel/sched/sched.cc')

    yield_start = sched.index('void yield()')
    yield_end = sched.index('void thread_exit()')
    yield_body = sched[yield_start:yield_end]

    assert 'rq_pop()' in yield_body
    assert 'rq_push(prev)' in yield_body
    assert 'switch_context(&prev->saved_sp, next->saved_sp)' in yield_body
    # No peer: keep running without corrupting the queue.
    assert 'if (next == nullptr)' in yield_body


def test_idle_thread_replaces_naked_kernel_halt_loop() -> None:
    kernel = read_source('src/kernel/kernel.cc')
    sched = read_source('src/kernel/sched/sched.cc')

    # kernel() must enter the scheduler instead of an unmanaged hlt loop.
    assert 'bigos::sched::start();' in kernel
    assert 'while (true) {\n        asm volatile("hlt");\n    }' not in kernel

    # start order: scheduler entered after IRQs are enabled.
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    start_index = kernel.index('bigos::sched::start();')
    assert enable_irq_index < start_index

    # idle thread owns halt behavior.
    start_start = sched.index('void start()')
    start_body = sched[start_start : sched.index('void on_timer_tick()')]
    assert 'ThreadState::Idle' in start_body
    assert 'sched::yield();' in start_body
    assert 'asm volatile("hlt")' in start_body


def test_kernel_init_order_is_unchanged_before_scheduler() -> None:
    kernel = read_source('src/kernel/kernel.cc')

    init_mem_index = kernel.index('bigos::init_mem(boot_info);')
    init_tty_index = kernel.index('bigos::terminal::init_tty();')
    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    start_index = kernel.index('bigos::sched::start();')

    assert init_mem_index < init_tty_index < init_irq_index < enable_irq_index < start_index


def test_timer_irq_records_bounded_intent_without_preemption() -> None:
    isr = read_source('src/kernel/irq/isr.cc')
    sched = read_source('src/kernel/sched/sched.cc')

    timer_body = isr[isr.index('implement_isr(timer)') : isr.index('implement_isr(keyboard)')]
    assert 'bigos::timer::on_tick();' in timer_body
    assert 'bigos::sched::on_timer_tick();' in timer_body

    # The hook only records bounded intent; no switch/alloc/io in IRQ context.
    hook_start = sched.index('void on_timer_tick()')
    hook_body = sched[hook_start:]
    assert 'g_reschedule_intent' in hook_body
    for token in ('switch_context', 'kmalloc', 'alloc_kernel_pages', 'free(', 'yield()'):
        assert token not in hook_body


def test_irq_paths_do_not_allocate_scheduler_objects() -> None:
    isr = read_source('src/kernel/irq/isr.cc')
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    timer_body = isr[isr.index('implement_isr(timer)') : isr.index('implement_isr(keyboard)')]
    keyboard_body = isr[isr.index('implement_isr(keyboard)') : isr.index('void init_isr_timer()')]
    page_fault_body = interrupt[
        interrupt.index('static void page_fault_handler') : interrupt.index('static void default_external_irq_handler')
    ]

    forbidden = (
        'kmalloc',
        'alloc_kernel_pages',
        'free_pages',
        'create_kernel_thread',
        'new ',
        'delete',
    )
    for body in (timer_body, keyboard_body, page_fault_body):
        for token in forbidden:
            assert token not in body

    # Exception path remains diagnostic-only, no scheduler recovery.
    assert 'thread' not in page_fault_body.lower()
    assert 'retry' not in page_fault_body


def test_scheduler_smoke_is_default_off_and_bounded() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('src/kernel/kernel.cc')

    option_index = xmake.index('option("scheduler_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_SCHEDULER_SMOKE")')
    assert option_index < default_index < define_index

    assert '#ifdef BIGOS_SCHEDULER_SMOKE' in kernel
    assert 'SCHEDULER_SMOKE_ITERATIONS = 3' in kernel
    assert 'create_kernel_thread(&scheduler_smoke_worker_a, nullptr);' in kernel
    assert 'create_kernel_thread(&scheduler_smoke_worker_b, nullptr);' in kernel
    assert 'BIGOS_SCHED_THREAD_A' in kernel
    assert 'BIGOS_SCHED_THREAD_B' in kernel


def test_blocking_primitives_are_intrusive_and_context_guarded() -> None:
    thread_h = read_source('include/bigos/thread.h')
    sched_h = read_source('include/bigos/sched.h')
    sched = read_source('src/kernel/sched/sched.cc')
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    assert 'Blocked' in thread_h
    assert 'Sleeping' in thread_h
    assert 'struct WaitQueue' in sched_h
    assert 'using WaitPredicate' in sched_h
    assert 'WAIT_TIMEOUT = -110' in sched_h
    for token in ('wait_next', 'wait_queue', 'sleep_next', 'deadline_tick', 'wait_result'):
        assert token in sched

    assert 'bool can_block() noexcept' in sched_h
    assert 'g_nonblocking_depth' in sched
    assert 'g_scheduler_critical_depth' in sched
    assert 'interrupts_enabled()' in sched
    assert 'NonblockingContextGuard nonblocking_guard' in interrupt


def test_wait_queue_wakeup_and_timeout_paths_are_allocation_free() -> None:
    sched = read_source('src/kernel/sched/sched.cc')

    wait_start = sched.index('int wait_queue_wait_until')
    wake_start = sched.index('uint32_t wake_one')
    tick_start = sched.index('void on_timer_tick()')
    wait_body = sched[wait_start:wake_start]
    wake_body = sched[wake_start:tick_start]
    tick_body = sched[tick_start:]

    assert 'wait_queue_push_locked(__queue, self);' in wait_body
    assert 'ThreadState::Blocked' in wait_body
    assert 'ThreadState::Sleeping' in wait_body
    assert 'sleep_push_locked(self);' in wait_body
    assert 'schedule_blocked_current_locked(self);' in wait_body
    assert 'wake_thread_locked(t, WAIT_OK)' in wake_body
    assert 'wait_queue_remove_locked(cur);' in tick_body
    assert 'cur->wait_result = WAIT_TIMEOUT;' in tick_body
    assert 'cur->state = ThreadState::Runnable;' in tick_body

    for body in (wait_body, wake_body, tick_body):
        for token in ('kmalloc', 'alloc_kernel_pages', 'free(', 'kprintf', 'serial_puts', 'mdelay'):
            assert token not in body


def test_blocking_smoke_is_default_off_and_deterministic() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('src/kernel/kernel.cc')

    option_index = xmake.index('option("blocking_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_BLOCKING_SMOKE")')
    assert option_index < default_index < define_index

    assert '#ifdef BIGOS_BLOCKING_SMOKE' in kernel
    assert 'blocking_smoke_reader' in kernel
    assert 'blocking_smoke_producer' in kernel
    assert 'terminal::enqueue_input(BLOCKING_SMOKE_CHAR)' in kernel
    for marker in (
        'BIGOS_BLOCKING_WAIT_BLOCKED',
        'BIGOS_BLOCKING_WAKE_SENT',
        'BIGOS_BLOCKING_WAIT_RESUMED',
        'BIGOS_BLOCKING_TIMEOUT_BLOCKED',
        'BIGOS_BLOCKING_TIMEOUT_EXPIRED',
        'BIGOS_BLOCKING_SMOKE_PASSED',
    ):
        assert marker in kernel
