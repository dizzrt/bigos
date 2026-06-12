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


def test_cmos_rtc_driver_reads_once_with_bounded_uip_and_validation() -> None:
    rtc_h = read_source('include/drivers/rtc/cmos_rtc.h')
    rtc = read_source('kernel/drivers/rtc/cmos_rtc.cc')

    # Ports 0x70 (index) / 0x71 (data) and the one-shot read-only API.
    assert 'CMOS_INDEX_PORT = 0x70' in rtc_h
    assert 'CMOS_DATA_PORT = 0x71' in rtc_h
    assert 'bool read_time(DateTime *__out) noexcept;' in rtc_h

    # UIP polling is bounded, status register B decodes BCD and 12/24-hour mode,
    # and the century is fixed at 2000.
    assert 'UIP_POLL_LIMIT' in rtc_h
    assert 'RTC_CENTURY_BASE = 2000' in rtc_h
    assert 'wait_update_not_in_progress' in rtc
    assert 'STATUS_A_UIP' in rtc
    assert 'STATUS_B_BINARY' in rtc
    assert 'STATUS_B_24_HOUR' in rtc
    assert 'bcd_to_binary' in rtc

    # Field range validation returns failure (no panic / no block).
    assert 'in_range(result.month, 1, 12)' in rtc
    assert 'in_range(result.hour, 0, 23)' in rtc
    for token in ('panic', 'kpanic', 'khalt'):
        assert token not in rtc


def test_cmos_rtc_does_not_register_irq8_or_poll_periodically() -> None:
    rtc = read_source('kernel/drivers/rtc/cmos_rtc.cc')

    # No IRQ8 registration, no enable_irq, no scheduler/blocking in the driver TU.
    for token in ('enable_irq', 'I8259_IRQ_RTC', 'register_handler', 'sleep_for', 'on_tick'):
        assert token not in rtc


def test_wall_clock_module_api_and_conversion() -> None:
    time_h = read_source('include/bigos/time.h')
    time_cc = read_source('kernel/core/time/time.cc')

    assert 'void init() noexcept;' in time_h
    assert 'int64_t boot_unix_time() noexcept;' in time_h
    assert 'int64_t current_unix_time() noexcept;' in time_h
    assert 'int64_t days_from_civil(' in time_h

    # current time = baseline + elapsed ticks / TIMER_HZ, read-only arithmetic.
    assert 'g_boot_unix_time + (int64_t)(elapsed / bigos::timer::TIMER_HZ)' in time_cc
    # init reads the RTC once and records the boot tick.
    assert 'driver::rtc::read_time(&dt)' in time_cc
    assert 'g_boot_tick = bigos::timer::ticks();' in time_cc


def test_wall_clock_degrades_deterministically_on_rtc_failure() -> None:
    time_h = read_source('include/bigos/time.h')
    time_cc = read_source('kernel/core/time/time.cc')

    assert 'RTC_INVALID_BASELINE = 0' in time_h
    assert 'BIGOS_RTC_INVALID' in time_h
    # On RTC failure: fixed baseline, emit marker, never panic / block / allocate.
    assert 'g_boot_unix_time = RTC_INVALID_BASELINE;' in time_cc
    assert 'RTC_INVALID_MARKER' in time_cc
    for token in ('kpanic', 'kmalloc', 'alloc_kernel_pages', 'sleep_for'):
        assert token not in time_cc


def test_time_init_runs_after_timer_and_before_proc_init() -> None:
    kernel = read_source('kernel/core/kernel.cc')

    enable_irq_index = kernel.index('bigos::irq::enableIRQ();')
    time_init_index = kernel.index('bigos::time::init();')
    proc_init_index = kernel.index('bigos::proc::init();')

    # Wall clock baseline established after the tick is running and before any
    # process is created.
    assert enable_irq_index < time_init_index < proc_init_index


def test_process_carries_identity_quad_and_start_timestamp() -> None:
    proc_h = read_source('include/bigos/proc.h')

    for token in (
        'uint32_t uid;',
        'uint32_t gid;',
        'uint32_t euid;',
        'uint32_t egid;',
        'int64_t start_unix_time;',
    ):
        assert token in proc_h


def test_identity_initialized_on_creation_and_inherited_on_fork() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    # Both non-fork creation paths default to root and record the wall clock.
    assert proc.count('__process->uid = bigos::cred::ROOT_UID;') == 2
    assert proc.count('__process->start_unix_time = bigos::time::current_unix_time();') == 2

    # fork inherits the identity quad field-by-field and stamps a fresh time.
    fork_body = proc[proc.index('int64_t fork_current(') : proc.index('int64_t install_fd_current(')]
    assert 'child->uid = parent->uid;' in fork_body
    assert 'child->gid = parent->gid;' in fork_body
    assert 'child->euid = parent->euid;' in fork_body
    assert 'child->egid = parent->egid;' in fork_body
    assert 'child->start_unix_time = bigos::time::current_unix_time();' in fork_body


def test_exec_preserves_identity_and_start_timestamp() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    # exec copies a fixed set of fields from `prepared` back into the process and
    # must NOT copy the identity quad or start timestamp (so they are preserved).
    exec_body = proc[proc.index('UserElfLoadError exec_current_from_elf_image(') : proc.index('void run_user_process(')]
    for token in ('process->uid =', 'process->euid =', 'process->start_unix_time ='):
        assert token not in exec_body


def test_credential_decision_primitives_are_pure() -> None:
    cred_h = read_source('include/bigos/cred.h')
    cred = read_source('kernel/core/proc/cred.cc')

    assert 'ROOT_UID = 0' in cred_h
    assert 'enum class Access' in cred_h
    assert 'S_IRUSR = 0400' in cred_h
    assert 'bool may_signal(' in cred_h
    assert 'bool permits(' in cred_h

    # may_signal: null -> reject, root -> allow, else identity match.
    may_body = cred[cred.index('bool may_signal(') : cred.index('bool permits(')]
    assert 'return false;' in may_body
    assert '__actor->euid == ROOT_UID' in may_body
    assert '__actor->euid == __target->uid' in may_body

    # permits: root bypass, owner/group/other selection, invalid access -> reject.
    permits_body = cred[cred.index('bool permits(') :]
    assert '__req_uid == ROOT_UID' in permits_body
    assert '__req_uid == __file_uid' in permits_body
    assert '__req_gid == __file_gid' in permits_body
    assert 'return false;' in permits_body

    # No side effects / no allocation / no panic in the decision TU.
    for token in ('kmalloc', 'kpanic', 'serial_puts', 'kputs', ' new ', 'delete '):
        assert token not in cred


def test_identity_time_syscall_numbers_appended_after_fork() -> None:
    syscall_h = read_source('include/bigos/syscall.h')

    assert 'SYS_FORK = 10' in syscall_h
    assert 'SYS_GET_TIME = 11' in syscall_h
    assert 'SYS_GETPID = 12' in syscall_h
    assert 'SYS_GETPPID = 13' in syscall_h
    assert 'SYS_GETUID = 14' in syscall_h
    assert 'SYS_GETGID = 15' in syscall_h


def test_identity_time_syscall_dispatch_branches() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    dispatch_body = syscall[syscall.index('void dispatch(') :]
    assert 'case SYS_GET_TIME:' in dispatch_body
    assert 'result = __detail::sys_get_time();' in dispatch_body
    assert 'case SYS_GETPID:' in dispatch_body
    assert 'case SYS_GETPPID:' in dispatch_body
    assert 'case SYS_GETUID:' in dispatch_body
    assert 'case SYS_GETGID:' in dispatch_body

    # SYS_GET_TIME returns the wall clock.
    assert 'bigos::time::current_unix_time()' in syscall

    # Dispatch result still flows through rax and no EOI is added.
    assert '__frame->rax = (uint64_t)result;' in syscall


def test_identity_time_syscalls_obey_interrupt_context_contract() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    # The new query helpers must not allocate, block, or send an EOI.
    for token in ('kmalloc', 'alloc_kernel_pages', 'free_pages', 'send_eoi'):
        assert token not in syscall


def test_time_identity_smoke_switch_and_marker_present() -> None:
    xmake = read_source('xmake.lua')
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')
    kernel = read_source('kernel/core/kernel.cc')

    option_index = xmake.index('option("time_identity_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_TIME_IDENTITY_SMOKE")')
    assert option_index < default_index < define_index

    # Existing smoke matrix preserved.
    assert 'option("fork_cow_smoke")' in xmake

    assert '#ifdef BIGOS_TIME_IDENTITY_SMOKE' in proc_h
    assert 'void time_identity_smoke_entry(void *) noexcept;' in proc_h

    assert 'BIGOS_TIME_IDENTITY_PASSED' in proc
    assert 'BIGOS_TIME_IDENTITY_FAILED' in proc
    assert 'time_identity_smoke_entry' in kernel
