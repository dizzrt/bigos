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


def test_process_core_builds_outside_smoke_and_initializes_normally() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'add_defines("BIGOS_USER_PROCESS")' in xmake
    assert 'add_files("$(projectdir)/kernel/core/proc/**.cc")' in xmake
    assert 'if has_config("user_program_smoke") then' in xmake
    assert 'add_defines("BIGOS_USER_PROGRAM_SMOKE")' in xmake
    assert 'if has_config("user_elf_smoke") then' in xmake
    assert 'add_defines("BIGOS_USER_ELF_SMOKE")' in xmake
    assert 'bigos::proc::init();' in kernel
    assert 'BIGOS_PROC_INIT' in proc


def test_bounded_pid_table_parent_child_and_publication_rules() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')

    # The former fixed 16-process array is replaced by an intrusive list bounded
    # by a configurable soft limit; PID/parent-child/publication rules are kept.
    for token in (
        'MAX_PROCESSES_SOFT_LIMIT = 1024',
        'ROOT_PARENT_PID = 0',
        'WAIT_ANY = 0xffffffffu',
        'g_process_list_head',
        'g_process_count >= bigos::proc::MAX_PROCESSES_SOFT_LIMIT',
        'uint32_t alloc_pid()',
        'lookup_process(pid) == nullptr',
        'bool publish_process',
        'void unpublish_process',
        'parent->first_child_pid = pid',
        '__process->table_published = true',
    ):
        assert token in proc_h + proc


def test_process_object_and_fd_storage_are_heap_allocated_and_growable() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')

    # Process objects come from the kernel heap and are freed on reap; fd storage
    # is a growable heap array, both bounded by configurable soft limits.
    assert 'FdEntry *fd_table;' in proc_h
    assert 'uint32_t fd_capacity;' in proc_h
    assert 'Process *reg_next;' in proc_h
    assert 'MAX_FDS_SOFT_LIMIT = 256' in proc_h
    assert 'alloc_process_object()' in proc
    assert 'free_process_object(process);' in proc
    assert 'grow_fd_table(' in proc
    assert 'free_fd_table(process);' in proc


def test_wait_exit_zombie_reap_and_nonblocking_context_rules() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')
    syscall_h = read_source('include/bigos/syscall.h')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    errno_h = read_source('include/bigos/errno.h')

    for token in (
        'Zombie',
        'ReapPending',
        'int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept;',
    ):
        assert token in proc_h

    # wait errors converge onto the single bigos/errno.h source (negated on the
    # way to the return register): ECHILD = -10, EWOULDBLOCK = -11.
    assert 'constexpr int ECHILD = 10;' in errno_h
    assert 'constexpr int EWOULDBLOCK = 11;' in errno_h
    assert 'return -bigos::ECHILD;' in proc
    assert 'return -bigos::EWOULDBLOCK;' in proc

    assert 'mark_zombie_or_reap_pending(process)' in proc
    assert 'bigos::sched::wake_all(&g_process_wait_queue)' in proc
    assert 'if (!bigos::sched::can_block())' in proc
    assert 'bigos::sched::wait_queue_wait_until(&g_process_wait_queue' in proc
    assert 'mark_reap_pending(match);' in proc
    assert 'SYS_WAIT = 4' in syscall_h
    assert 'bigos::proc::wait_current((uint32_t)__frame->rdi, nullptr)' in syscall
