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


def test_user_program_smoke_is_default_off_and_wired_to_scheduler() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    option_index = xmake.index('option("user_program_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_USER_PROGRAM_SMOKE")')
    assert option_index < default_index < define_index

    assert '#ifdef BIGOS_USER_PROGRAM_SMOKE' in kernel
    assert 'bigos::proc::user_program_smoke_entry' in kernel
    assert 'bigos::sched::start();' in kernel


def test_flat_embedded_image_has_no_fs_or_block_dependency() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    doc = read_source('docs/en/arch/first-user-program.md')

    assert 'FIRST_USER_CODE' in proc
    assert 'Flat embedded image' in proc
    assert 'BIGOS_USER_WRITE' in proc
    assert 'flat blob' in doc
    blob_section = proc[
        proc.index('constexpr uint8_t FIRST_USER_CODE') : proc.index('bigos::proc::Process *g_current_process')
    ]
    for token in ('open(', 'read(', 'exfat', 'block-device'):
        assert token not in blob_section


def test_process_model_records_identity_address_space_stack_and_exit_state() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')

    for token in (
        'uint32_t pid;',
        'uint32_t parent_pid;',
        'uint32_t first_child_pid;',
        'uint64_t address_space_root;',
        'uint64_t kernel_address_space_root;',
        'uint64_t entry;',
        'UserRange stack;',
        'uint64_t initial_stack;',
        'ProcessState state;',
        'bool table_published;',
        'int64_t exit_code;',
    ):
        assert token in proc_h

    assert 'alloc_pid()' in proc
    assert 'g_process_list_head' in proc
    assert 'publish_process(__process, ROOT_PARENT_PID)' in proc
    assert '__process->state = ProcessState::Created;' in proc
    assert 'process->state = ProcessState::Terminated;' in proc
    assert 'process->exit_code = __code;' in proc


def test_loader_maps_code_data_bss_and_stack_with_user_attributes() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'derive_user_address_space_root()' in proc
    assert 'map_page_in_root(' in proc
    assert 'USER_CODE_BASE' in proc and 'USER_DATA_BASE' in proc and 'USER_STACK_TOP' in proc
    assert 'bigos::mm::page_attr::USER_CODE' in proc
    assert proc.count('bigos::mm::page_attr::USER_DATA') >= 2
    assert 'BIGOS_USER_LOAD_FAILED' in proc


def test_x86_user_mode_keeps_kernel_selectors_and_builds_iret_frame() -> None:
    user_mode_h = read_source('include/bigos/user_mode.h')
    user_mode_cc = read_source('kernel/core/proc/user_mode.cc')
    user_mode_s = read_source('kernel/core/proc/user_mode.s')

    assert 'KERNEL_CODE_SELECTOR = 0x08' in user_mode_h
    assert 'KERNEL_DATA_SELECTOR = 0x10' in user_mode_h
    assert 'KERNEL_STACK_SELECTOR = 0x18' in user_mode_h
    assert 'USER_DATA_SELECTOR = 0x23' in user_mode_h
    assert 'USER_CODE_SELECTOR = 0x2b' in user_mode_h
    assert 'TSS_SELECTOR = 0x30' in user_mode_h
    assert 'g_tss.rsp0 = __rsp0;' in user_mode_cc
    assert 'iretq' in user_mode_s


def test_syscall_gate_user_buffer_exit_and_fault_boundaries() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'PRESENT_RING3_TRAP_GATE = 0xef00' in interrupt
    assert 'if (i == VECTOR_SYSCALL)' in interrupt
    assert 'driver::irqchip::i8259::send_eoi' in interrupt

    syscall_start = interrupt.index('if (__detail::is_syscall_vector(__frame->vector))')
    syscall_body = interrupt[
        syscall_start : interrupt.index('__detail::unknown_vector_handler(__frame);', syscall_start)
    ]
    assert 'send_eoi' not in syscall_body

    assert 'validate_user_buffer(__buffer, __len)' in syscall
    # The SYS_WRITE_MAX_LEN bounded buffer now lives in the terminal write op
    # (device/handle layering); the syscall write path uses the SYS_IO_MAX_LEN
    # bounded copy shared by all fd-table writes.
    tty = read_source('kernel/core/terminal/tty.cc')
    assert 'bounded[bigos::sys::SYS_WRITE_MAX_LEN + 1]' in tty
    assert 'char bounded[SYS_IO_MAX_LEN];' in syscall
    assert 'bigos::proc::exit_current((int64_t)__frame->rdi);' in syscall
    assert 'user_mode = (__frame->cs & 0x3) == 0x3' in interrupt
    assert 'bigos::proc::fault_current_and_exit(-14);' in interrupt
    assert 'bigos::serial_puts("BIGOS_USER_PAGE_FAULT\\n");' in proc
    assert 'bigos::mm::user_range_mapped(process->address_space_root, __addr, __len)' in proc


def test_layout_abi_constants_are_not_moved() -> None:
    link = read_source('link.lds')
    vmem = read_source('kernel/mm/vmem.cc')
    interrupt_h = read_source('include/irq/interrupt.h')

    assert '. = 0xffffffff80000000;' in link
    assert '#define KVMEM_BASE       0xffff880000000000ul' in vmem
    assert '#define KERNEL_HIGHER_HALF_BASE 0xffffffff80000000ul' in vmem
    assert '#define SELF_MAPPING_BASE       0xffff800000000000ul' in vmem
    assert 'constexpr uintptr_t KDIRECT_BASE = 0xffff900000000000ul;' in read_source('include/bigos/memory.h')
    assert 'struct InterruptFrame' in interrupt_h
    assert 'uint64_t rip;' in interrupt_h and 'uint64_t cs;' in interrupt_h and 'uint64_t rflags;' in interrupt_h
