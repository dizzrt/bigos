from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_scheduler_uses_cpu_owned_domains_and_explicit_placement() -> None:
    sched_h = read_source('include/bigos/sched.h')
    sched = read_source('kernel/core/sched/sched.cc')

    assert 'ThreadId create_kernel_thread_on_cpu' in sched_h
    assert 'SchedulerCpuState g_domains[cpu::MAX_CPUS]' in sched
    assert 'cpu_id' in sched and 'run_queue_depth' in sched
    assert 'cpu_schedulable' in sched
    assert 'select_placement_cpu' in sched
    assert 'rq_push(SchedulerCpuState *__domain' in sched
    assert 'rq_pop(SchedulerCpuState *__domain' in sched
    assert 'domain->terminated_head' in sched


def test_remote_enqueue_publication_precedes_scheduler_nudge() -> None:
    sched = read_source('kernel/core/sched/sched.cc')

    create_body = sched[sched.index('ThreadId create_kernel_thread_target') : sched.index('void yield()')]
    assert create_body.index('rq_push(target, tcb);') < create_body.index('request_reschedule_locked(target);')
    assert create_body.index('request_reschedule_locked(target);') < create_body.index('nudge_cpu(__target_cpu);')
    assert 'driver::irqchip::lapic::send_fixed_ipi' in sched
    assert 'irq::VECTOR_SCHED_NUDGE' in sched


def test_scheduler_nudge_vector_is_lapic_scoped_not_generic_ipi() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    isr = read_source('kernel/core/irq/isr.cc')
    lapic_h = read_source('include/drivers/irqchip/lapic.h')

    assert 'VECTOR_SCHED_NUDGE' in interrupt_h
    assert 'VECTOR_LAPIC_TIMER || __vector == VECTOR_SCHED_NUDGE' in interrupt
    assert 'register_isr(VECTOR_SCHED_NUDGE, &__detail::isr_scheduler_nudge)' in isr
    assert 'bigos::sched::on_scheduler_nudge();' in isr
    assert 'send_fixed_ipi' in lapic_h


def test_ap_tick_and_irq_return_use_cpu_local_scheduler_domain() -> None:
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    sched = read_source('kernel/core/sched/sched.cc')

    ap_timer_branch = isr[isr.index('if (cpu_id != bigos::cpu::BOOTSTRAP_CPU_ID)') : isr.index('// Advance')]
    assert 'bigos::sched::on_timer_tick();' in ap_timer_branch
    assert 'g_boot_cpu_sched' not in sched
    assert 'current_domain()' in sched

    lapic_branch = interrupt[interrupt.index('if (__detail::is_lapic_external_irq') : interrupt.index('if (__detail::is_syscall_vector')]
    assert 'bigos::cpu::is_bootstrap_cpu()' not in lapic_branch
    assert 'bigos::sched::maybe_preempt_on_irq_return(__frame);' in lapic_branch


def test_multicore_scheduler_smoke_is_default_off_and_bounded() -> None:
    options = read_source('xmake/options.lua')
    kernel_lua = read_source('xmake/kernel.lua')
    kernel = read_source('kernel/core/kernel.cc')
    boot_debug = read_source('tools/boot_debug.py')

    option_index = options.index('option("scheduler_smp_smoke")')
    default_index = options.index('set_default(false)', option_index)
    assert option_index < default_index
    assert 'add_defines("BIGOS_AP_STARTUP_PERCPU_TIMERS")' in kernel_lua
    assert 'add_defines("BIGOS_SCHEDULER_SMP_SMOKE")' in kernel_lua

    assert 'BIGOS_SCHED_SMP_BSP_THREAD' in kernel
    assert 'BIGOS_SCHED_SMP_AP_THREAD' in kernel
    assert 'BIGOS_SCHED_SMP_PASSED' in kernel
    assert 'create_kernel_thread_on_cpu(&scheduler_smp_ap_worker' in kernel

    assert "case_id='scheduler-smp'" in boot_debug
    assert "'scheduler_smp_smoke'" in boot_debug
    assert "qemu_extra=('-cpu', 'max', '-smp', '2')" in boot_debug
