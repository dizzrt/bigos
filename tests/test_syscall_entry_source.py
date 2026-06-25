import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/common.lua',
            ROOT / 'xmake/boot_artifacts.lua',
            ROOT / 'xmake/user_package.lua',
            ROOT / 'xmake/runtime.lua',
            ROOT / 'xmake/kernel.lua',
            ROOT / 'xmake/run_targets.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def parse_kernel_enum_assignments(source: str) -> dict[str, int]:
    enum_start = source.index('enum SyscallNumber')
    enum_end = source.index('};', enum_start)
    enum_body = source[enum_start:enum_end]
    return {name: int(value) for name, value in re.findall(r'\b(SYS_[A-Z0-9_]+)\s*=\s*(\d+)', enum_body)}


def parse_kernel_errno_constants(source: str) -> dict[str, int]:
    return {name: int(value) for name, value in re.findall(r'constexpr int (E[A-Z0-9_]+)\s*=\s*(\d+);', source)}


def parse_user_defines(source: str) -> dict[str, int]:
    return {name: int(value) for name, value in re.findall(r'^#define\s+([A-Z][A-Z0-9_]+)\s+(\d+)$', source, re.M)}


def extract_c_function(source: str, signature: str) -> str:
    start = source.index(signature)
    body_start = source.index('{', start)
    depth = 0
    for index in range(body_start, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f'function body not found for {signature}')


def test_syscall_vector_constant_is_named_and_centralized() -> None:
    interrupt_h = read_source('include/irq/interrupt.h')

    assert 'VECTOR_SYSCALL = 0x80' in interrupt_h
    assert 'DPL=3' in interrupt_h
    assert 'exception' in interrupt_h and 'external IRQ gates remain ring0-only' in interrupt_h


def test_syscall_abi_is_declared_and_documented() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    errno_h = read_source('include/bigos/errno.h')

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

    # syscall number enum, error code source, and dispatch entry declaration.
    assert 'SYS_DEBUG_WRITE = 0' in syscall_h
    assert 'SYS_GET_TICK = 1' in syscall_h
    assert 'SYS_WRITE = 2' in syscall_h
    assert 'SYS_EXIT = 3' in syscall_h
    # POSIX-style error codes are centralized in bigos/errno.h as positive
    # values; the dispatcher negates them on the way to the return register.
    assert '#include <bigos/errno.h>' in syscall_h
    assert 'constexpr int ENOSYS = 38;' in errno_h
    assert 'constexpr int EFAULT = 14;' in errno_h
    assert 'void dispatch(bigos::irq::InterruptFrame *__frame) noexcept;' in syscall_h


def test_user_libc_syscall_and_errno_mirrors_match_kernel_headers() -> None:
    kernel_syscalls = parse_kernel_enum_assignments(read_source('include/bigos/syscall.h'))
    user_syscalls = parse_user_defines(read_source('user/libc/include/sys_nr.h'))
    kernel_errno = parse_kernel_errno_constants(read_source('include/bigos/errno.h'))
    user_errno = parse_user_defines(read_source('user/libc/include/errno.h'))

    assert user_syscalls == kernel_syscalls
    assert user_errno == kernel_errno
    assert user_syscalls['SYS_EXECVE'] == 27
    assert user_syscalls['SYS_WAITPID'] == 47
    assert user_syscalls['SYS_FCNTL'] == 48
    assert user_syscalls['SYS_ACCESS'] == 49
    assert user_syscalls['SYS_TRUNCATE'] == 50
    assert user_errno['ENOENT'] == 2
    assert user_errno['E2BIG'] == 7
    assert user_errno['ENOEXEC'] == 8


def test_user_crt0_exit_number_matches_shared_syscall_mirror() -> None:
    crt0 = read_source('user/crt0/crt0.s')
    user_syscalls = parse_user_defines(read_source('user/libc/include/sys_nr.h'))

    assert '.equ SYS_EXIT, 3' in crt0
    assert 'movq $SYS_EXIT, %rax' in crt0
    assert user_syscalls['SYS_EXIT'] == 3


def test_user_raw_syscall_primitives_follow_register_contract() -> None:
    syscall_c = read_source('user/libc/syscall.c')

    for nr_args in range(7):
        params = ', '.join(['long n', *[f'long a{i}' for i in range(nr_args)]])
        body = extract_c_function(syscall_c, f'long syscall{nr_args}({params})')

        assert '"int $0x80"' in body
        assert '"=a"(ret)' in body
        assert '"a"(n)' in body
        assert '"rcx", "r11", "memory"' in body

        if nr_args >= 1:
            assert '"D"(a0)' in body
        if nr_args >= 2:
            assert '"S"(a1)' in body
        if nr_args >= 3:
            assert '"d"(a2)' in body
        if nr_args >= 4:
            assert 'register long r10 __asm__("r10") = a3;' in body
            assert '"r"(r10)' in body
        if nr_args >= 5:
            assert 'register long r8 __asm__("r8") = a4;' in body
            assert '"r"(r8)' in body
        if nr_args >= 6:
            assert 'register long r9 __asm__("r9") = a5;' in body
            assert '"r"(r9)' in body


def test_syscall_abi_mapping_is_documented_in_arch_docs() -> None:
    docs = read_source('docs/en/arch/syscall-entry.md')

    assert 'int 0x80' in docs
    assert '`rax`' in docs
    assert '`rdi`' in docs and '`r10`' in docs
    assert 'InterruptFrame' in docs
    assert '-bigos::ENOSYS' in docs


def test_irq_dispatch_recognizes_syscall_vector_without_eoi() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')

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
    syscall = read_source('kernel/core/syscall/syscall.cc')

    # number read from InterruptFrame.rax, result written back to InterruptFrame.rax.
    assert 'const uint64_t number = __frame->rax;' in syscall
    assert '__frame->rax = (uint64_t)result;' in syscall

    # known numbers route to implementations.
    assert 'case SYS_DEBUG_WRITE:' in syscall
    assert 'result = __detail::sys_debug_write();' in syscall
    assert 'case SYS_GET_TICK:' in syscall
    assert 'result = __detail::sys_get_tick();' in syscall
    assert 'case SYS_WRITE:' in syscall
    assert 'result = __detail::sys_write(__frame->rdi, __frame->rsi, __frame->rdx);' in syscall
    assert 'case SYS_EXIT:' in syscall
    assert 'bigos::proc::exit_current((int64_t)__frame->rdi);' in syscall
    assert 'case SYS_WAITPID:' in syscall
    assert 'sys_waitpid(__frame->rdi, __frame->rsi, __frame->rdx)' in syscall
    assert 'case SYS_FCNTL:' in syscall
    assert 'sys_fcntl(__frame->rdi, __frame->rsi, __frame->rdx)' in syscall
    assert 'case SYS_ACCESS:' in syscall
    assert 'sys_access(__frame->rdi, __frame->rsi)' in syscall
    assert 'case SYS_TRUNCATE:' in syscall
    assert 'sys_truncate(__frame->rdi, __frame->rsi)' in syscall


def test_syscall_dispatch_unknown_number_returns_deterministic_error() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    default_index = syscall.index('default:')
    enosys_index = syscall.index('result = -bigos::ENOSYS;', default_index)
    assert default_index < enosys_index

    # No crash / no exception path from the dispatcher.
    for token in ('khalt', 'panic', 'halt_cpu'):
        assert token not in syscall


def test_debug_write_syscall_emits_marker_from_bounded_kernel_buffer() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert 'BIGOS_SYSCALL_WRITE' in syscall
    assert 'serial_puts(kernel_buffer);' in syscall
    # The buffer is a fixed kernel-internal source, not derived from a pointer arg.
    assert 'not derived from any caller pointer' in syscall
    # ring3 prerequisite recorded in code.
    assert 'MUST be validated' in syscall


def test_get_tick_syscall_returns_monotonic_tick_via_return_register() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert 'sys_get_tick' in syscall
    assert 'bigos::timer::ticks()' in syscall


def test_syscall_path_obeys_interrupt_context_contract() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

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
    syscall = read_source('kernel/core/syscall/syscall.cc')
    kernel = read_source('kernel/core/kernel.cc')
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    # Syscall dispatch itself does not perform ring3 entry or load user code.
    for source in (syscall, interrupt):
        assert 'enter_user_mode' not in source
        assert 'swapgs' not in source
        assert 'load_user_elf' not in source

    # CR3 activation is owned by proc/vmem helpers, not the syscall smoke path.
    assert '%%cr3' not in syscall
    assert 'user_program_smoke_entry' in kernel


def test_stage_does_not_change_idt_dpl_or_add_user_gdt_tss_or_syscall_msr() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    # Only syscall vector gets the DPL=3 trap gate, preserving IF for fd/VFS
    # syscalls that pass sched::can_block() in ordinary process context.
    assert 'PRESENT_RING0_INTERRUPT_GATE = 0x8e00' in interrupt
    assert 'PRESENT_RING3_TRAP_GATE = 0xef00' in interrupt
    assert 'if (i == VECTOR_SYSCALL)' in interrupt

    # No syscall/sysret MSR configuration introduced.
    for source in (interrupt, syscall):
        assert 'IA32_STAR' not in source
        assert 'LSTAR' not in source
        assert 'sysret' not in source


def test_syscall_smoke_is_default_off_gated_and_bounded() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

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
    assert 'unknown_ret == -bigos::ENOSYS' in kernel


def test_syscall_smoke_runs_in_non_interrupt_context_after_irq_init() -> None:
    kernel = read_source('kernel/core/kernel.cc')

    init_irq_index = kernel.index('bigos::irq::initIRQ();')
    smoke_call_index = kernel.index('syscall_smoke();')
    start_index = kernel.index('bigos::sched::start();')

    # Self-test runs after the IDT is loaded and before the scheduler takes over.
    assert init_irq_index < smoke_call_index < start_index
