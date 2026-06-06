from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_syscall_vector_constant_is_named_and_centralized() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')

    assert 'VECTOR_SYSCALL = 0x80' in interrupt_h
    # DPL stays at ring0 this stage; raising it is an explicit later ring3 change.
    assert 'DPL=0' in interrupt_h
    assert 'ring3 change' in interrupt_h


def test_syscall_abi_is_declared_and_documented() -> None:
    syscall_h = read_source('include/bigos/syscall.h')

    # number / return value register convention.
    assert 'syscall number -> rax' in syscall_h
    assert 'return value   -> rax' in syscall_h

    # fixed argument register order rdi/rsi/rdx/r10/r8/r9.
    assert 'argument 0     -> rdi' in syscall_h
    assert 'argument 1     -> rsi' in syscall_h
    assert 'argument 2     -> rdx' in syscall_h
    assert 'argument 3     -> r10' in syscall_h
    assert 'argument 4     -> r8' in syscall_h
    assert 'argument 5     -> r9' in syscall_h

    # syscall number enum, error code, and dispatch entry declaration.
    assert 'SYS_DEBUG_WRITE = 0' in syscall_h
    assert 'SYS_GET_TICK = 1' in syscall_h
    assert 'SYS_ENOSYS = -38' in syscall_h
    assert 'void dispatch(bigos::irq::InterruptFrame *__frame) noexcept;' in syscall_h


def test_syscall_abi_mapping_is_documented_in_arch_docs() -> None:
    docs = read_source('docs/arch/syscall-entry.md')

    assert 'int 0x80' in docs
    assert '`rax`' in docs
    assert '`rdi`' in docs and '`r10`' in docs
    assert 'InterruptFrame' in docs
    assert 'SYS_ENOSYS = -38' in docs


def test_irq_dispatch_recognizes_syscall_vector_without_eoi() -> None:
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    assert 'is_syscall_vector' in interrupt
    assert '__vector == VECTOR_SYSCALL' in interrupt

    syscall_start = interrupt.index('if (__detail::is_syscall_vector(__frame->vector))')
    syscall_end = interrupt.index('unknown_vector_handler(__frame);', syscall_start)
    syscall_body = interrupt[syscall_start:syscall_end]

    # Syscall path routes to the dispatcher and must NOT send an i8259 EOI.
    assert 'bigos::sys::dispatch(__frame);' in syscall_body
    assert 'send_eoi' not in syscall_body

    # External IRQ branch still owns exactly one EOI; syscall branch added none.
    assert interrupt.count('driver::irqchip::i8259::send_eoi') == 1


def test_syscall_dispatch_reads_rax_and_routes_known_numbers() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')

    # number read from InterruptFrame.rax, result written back to InterruptFrame.rax.
    assert 'const uint64_t number = __frame->rax;' in syscall
    assert '__frame->rax = (uint64_t)result;' in syscall

    # known numbers route to implementations.
    assert 'case SYS_DEBUG_WRITE:' in syscall
    assert 'result = __detail::sys_debug_write();' in syscall
    assert 'case SYS_GET_TICK:' in syscall
    assert 'result = __detail::sys_get_tick();' in syscall


def test_syscall_dispatch_unknown_number_returns_deterministic_error() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')

    default_index = syscall.index('default:')
    enosys_index = syscall.index('result = SYS_ENOSYS;', default_index)
    assert default_index < enosys_index

    # No crash / no exception path from the dispatcher.
    for token in ('khalt', 'panic', 'halt_cpu'):
        assert token not in syscall


def test_debug_write_syscall_emits_marker_from_bounded_kernel_buffer() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')

    assert 'BIGOS_SYSCALL_WRITE' in syscall
    assert 'serial_puts(kernel_buffer);' in syscall
    # The buffer is a fixed kernel-internal source, not derived from a pointer arg.
    assert 'not derived from any caller pointer' in syscall
    # ring3 prerequisite recorded in code.
    assert 'MUST be validated' in syscall


def test_get_tick_syscall_returns_monotonic_tick_via_return_register() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')

    assert 'sys_get_tick' in syscall
    assert 'bigos::timer::ticks()' in syscall


def test_syscall_path_obeys_interrupt_context_contract() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')

    forbidden_tokens = (
        'kmalloc',
        'alloc_kernel_pages',
        'alloc_physical_order',
        'free_pages',
        'free(',
        ' new ',
        'delete ',
    )
    for token in forbidden_tokens:
        assert token not in syscall


def test_stage_does_not_enter_ring3_or_switch_cr3() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')
    kernel = read_source('src/kernel/kernel.cc')
    interrupt = read_source('src/kernel/irq/interrupt.cc')

    # No ring3 entry / CR3 switch / user ELF load wired into the syscall path.
    for source in (syscall, kernel, interrupt):
        assert 'iret_to_user' not in source
        assert 'swapgs' not in source
        assert 'load_user_elf' not in source

    # No CR3 write in the syscall implementation or kernel syscall smoke path.
    assert 'movq %0, %%cr3' not in syscall
    assert '%%cr3' not in syscall


def test_stage_does_not_change_idt_dpl_or_add_user_gdt_tss_or_syscall_msr() -> None:
    interrupt = read_source('src/kernel/irq/interrupt.cc')
    syscall = read_source('src/kernel/syscall/syscall.cc')

    # IDT gate attribute is unchanged (still the ring0 interrupt gate).
    assert 'PRESENT_RING0_INTERRUPT_GATE = 0x8e00' in interrupt
    assert '0xee00' not in interrupt   # would be a DPL=3 interrupt gate

    # No syscall/sysret MSR configuration introduced.
    for source in (interrupt, syscall):
        assert 'IA32_STAR' not in source
        assert 'LSTAR' not in source
        assert 'sysret' not in source


def test_syscall_smoke_is_default_off_gated_and_bounded() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('src/kernel/kernel.cc')

    option_index = xmake.index('option("syscall_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_SYSCALL_SMOKE")')
    assert option_index < default_index < define_index

    # Smoke wiring is guarded and emits deterministic markers.
    assert '#ifdef BIGOS_SYSCALL_SMOKE' in kernel
    assert 'int $0x80' in kernel
    assert 'BIGOS_SYSCALL_SMOKE_PASSED' in kernel
    assert 'BIGOS_SYSCALL_SMOKE_FAILED' in kernel
    # Asserts dispatcher hit, correct return value, and unknown-number error code.
    assert 'unknown_ret == bigos::sys::SYS_ENOSYS' in kernel


def test_syscall_smoke_runs_in_non_interrupt_context_after_irq_init() -> None:
    kernel = read_source('src/kernel/kernel.cc')

    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    smoke_call_index = kernel.index('syscall_smoke();')
    start_index = kernel.index('bigos::sched::start();')

    # Self-test runs after the IDT is loaded and before the scheduler takes over.
    assert init_irq_index < smoke_call_index < start_index
