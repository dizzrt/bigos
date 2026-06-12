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


def test_signal_numbers_and_bitmap_width() -> None:
    sig_h = read_source('include/bigos/signal.h')

    # Fixed non-realtime numbers reuse conventional POSIX values.
    assert 'SIGKILL = 9' in sig_h
    assert 'SIGUSR1 = 10' in sig_h
    assert 'SIGSEGV = 11' in sig_h
    assert 'SIGTERM = 15' in sig_h
    assert 'SIGCHLD = 17' in sig_h

    # The bitmap type and the highest signal number must agree (<= 64).
    assert 'using SigSet = uint64_t;' in sig_h
    assert 'SIG_MAX = 31' in sig_h
    assert 'SIG_COUNT = SIG_MAX' in sig_h


def test_signal_syscall_numbers_appended_after_getgid() -> None:
    syscall_h = read_source('include/bigos/syscall.h')

    assert 'SYS_GETGID = 15' in syscall_h
    assert 'SYS_KILL = 16' in syscall_h
    assert 'SYS_SIGACTION = 17' in syscall_h
    assert 'SYS_SIGPROCMASK = 18' in syscall_h
    assert 'SYS_SIGRETURN = 19' in syscall_h


def test_errno_has_eperm_and_esrch_single_source() -> None:
    errno_h = read_source('include/bigos/errno.h')

    assert 'EPERM = 1' in errno_h
    assert 'ESRCH = 3' in errno_h
    # No duplicate definitions in subsystem sources.
    for rel in ('kernel/core/signal/signal.cc', 'kernel/core/syscall/syscall.cc'):
        src = read_source(rel)
        assert 'EPERM =' not in src
        assert 'ESRCH =' not in src


def test_process_carries_signal_state_fields() -> None:
    proc_h = read_source('include/bigos/proc.h')

    for token in (
        'bigos::signal::SigSet sig_pending;',
        'bigos::signal::SigSet sig_mask;',
        'bigos::signal::SigDisposition sig_disp[bigos::signal::SIG_COUNT];',
    ):
        assert token in proc_h


def test_sigkill_uncatchable_and_unblockable() -> None:
    sig = read_source('kernel/core/signal/signal.cc')

    # set_disposition rejects a non-default disposition for an uncatchable signal.
    set_disp = sig[sig.index('int64_t set_disposition(') : sig.index('int64_t set_mask(')]
    assert 'is_uncatchable(__signo) && __new->action != SigAction::Default' in set_disp
    assert '-bigos::EINVAL' in set_disp

    # set_mask drops the unblockable bit from any requested set.
    set_mask = sig[sig.index('int64_t set_mask(') : sig.index('void deliver_pending_to_user(')]
    assert 'signo_bit(SIGKILL)' in set_mask
    assert '__set & ~unblockable' in set_mask

    # Delivery treats SIGKILL as always-terminate regardless of disposition.
    deliver = sig[sig.index('void deliver_pending_to_user(') : sig.index('void sigreturn(')]
    assert 'if (is_uncatchable(signo)) {' in deliver


def test_kill_sets_pending_without_permission_or_allocation() -> None:
    sig = read_source('kernel/core/signal/signal.cc')

    kill_body = sig[sig.index('int64_t kill(') : sig.index('bool has_deliverable_signal(')]
    assert '__target->sig_pending |= signo_bit(__signo);' in kill_body
    assert 'may_signal' not in kill_body  # permission is enforced by the syscall layer

    # No allocation / blocking anywhere in the signal TU.
    for token in ('kmalloc', 'alloc_kernel_pages', 'free_pages', 'sleep_for', 'send_eoi'):
        assert token not in sig


def test_kill_syscall_enforces_may_signal_and_errno_ordering() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    kill_body = syscall[syscall.index('static int64_t sys_kill(') : syscall.index('static int64_t sys_sigaction(')]
    # ESRCH for absent target precedes the permission decision; EPERM on denial.
    esrch_index = kill_body.index('-bigos::ESRCH')
    may_index = kill_body.index('bigos::cred::may_signal(actor, target)')
    eperm_index = kill_body.index('-bigos::EPERM')
    assert esrch_index < may_index < eperm_index
    # The kill primitive is only reached after the permission check passes.
    assert kill_body.index('bigos::signal::kill(target') > eperm_index


def test_irq_return_delivery_point_is_user_frame_only() -> None:
    interrupt = read_source('kernel/core/irq/interrupt.cc')

    # Delivery happens after maybe_preempt_on_irq_return, gated on a user frame.
    preempt_index = interrupt.index('maybe_preempt_on_irq_return(__frame);')
    deliver_index = interrupt.index('bigos::signal::deliver_pending_to_user(__frame, current)')
    assert preempt_index < deliver_index
    gate = interrupt[preempt_index:deliver_index]
    assert '(__frame->cs & 0x3) == 0x3' in gate
    assert 'has_deliverable_signal(current)' in gate


def test_sigreturn_forces_user_constraints() -> None:
    sig = read_source('kernel/core/signal/signal.cc')

    sigreturn = sig[sig.index('void sigreturn(') :]
    # Magic check, user-low-half rip/rsp constraint, forced user segments and IF.
    assert 'SIGFRAME_MAGIC' in sigreturn
    assert 'USER_LOW_HALF_LIMIT' in sigreturn
    assert 'USER_CODE_SELECTOR' in sigreturn
    assert 'USER_DATA_SELECTOR' in sigreturn
    assert 'RFLAGS_IF' in sigreturn


def test_sigreturn_dispatch_does_not_overwrite_restored_rax() -> None:
    syscall = read_source('kernel/core/syscall/syscall.cc')

    dispatch = syscall[syscall.index('void dispatch(') :]
    # SYS_SIGRETURN returns before the shared __frame->rax write-back.
    sigreturn_index = dispatch.index('case SYS_SIGRETURN:')
    rax_writeback_index = dispatch.index('__frame->rax = (uint64_t)result;')
    assert sigreturn_index < rax_writeback_index
    branch = dispatch[sigreturn_index:rax_writeback_index]
    assert 'bigos::signal::sigreturn(__frame);' in branch
    assert 'return;' in branch


def test_lifecycle_initializes_inherits_and_resets_signal_state() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    # Both non-fork creation paths initialize signal state.
    assert proc.count('bigos::signal::init_state(__process);') == 2

    # fork inherits the disposition table and mask, clears pending.
    fork_body = proc[proc.index('int64_t fork_current(') : proc.index('int64_t install_fd_current(')]
    assert 'bigos::signal::inherit_on_fork(child, parent);' in fork_body

    # exec resets user handlers to default (mask/pending preserved).
    exec_body = proc[proc.index('UserElfLoadError exec_current_from_elf_image(') : proc.index('void run_user_process(')]
    assert 'bigos::signal::reset_handlers_on_exec(process);' in exec_body


def test_child_exit_sets_parent_sigchld() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    zombie_body = proc[
        proc.index('void mark_zombie_or_reap_pending(') : proc.index('bool clone_process_kernel_stack_mapping(')
    ]
    assert 'bigos::signal::kill(parent, bigos::signal::SIGCHLD)' in zombie_body


def test_signal_smoke_switch_and_marker_present() -> None:
    xmake = read_source('xmake.lua')
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')
    kernel = read_source('kernel/core/kernel.cc')

    option_index = xmake.index('option("signal_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_SIGNAL_SMOKE")')
    assert option_index < default_index < define_index

    # Existing smoke matrix preserved.
    assert 'option("time_identity_smoke")' in xmake
    assert 'kernel/core/signal/**.cc' in xmake

    assert '#ifdef BIGOS_SIGNAL_SMOKE' in proc_h
    assert 'void signal_smoke_entry(void *) noexcept;' in proc_h

    assert 'BIGOS_SIGNAL_PASSED' in proc
    assert 'BIGOS_SIGNAL_FAILED' in proc
    assert 'signal_smoke_entry' in kernel
