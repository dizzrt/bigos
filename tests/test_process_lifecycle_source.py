from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_process_core_builds_outside_smoke_and_initializes_normally() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('src/kernel/kernel.cc')
    proc = read_source('src/kernel/proc/proc.cc')

    assert 'add_defines("BIGOS_USER_PROCESS")' in xmake
    assert 'add_files("src/kernel/proc/**.cc")' in xmake
    assert 'if has_config("user_program_smoke") then\n        add_defines("BIGOS_USER_PROGRAM_SMOKE")' in xmake
    assert 'if has_config("user_elf_smoke") then\n        add_defines("BIGOS_USER_ELF_SMOKE")' in xmake
    assert 'bigos::proc::init();' in kernel
    assert 'BIGOS_PROC_INIT' in proc


def test_bounded_pid_table_parent_child_and_publication_rules() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('src/kernel/proc/proc.cc')

    for token in (
        'MAX_PROCESSES = 16',
        'ROOT_PARENT_PID = 0',
        'WAIT_ANY = 0xffffffffu',
        'g_process_table[bigos::proc::MAX_PROCESSES]',
        'uint32_t alloc_pid()',
        'lookup_process(pid) == nullptr',
        'bool publish_process',
        'void unpublish_process',
        'parent->first_child_pid = pid',
        '__process->table_published = true',
    ):
        assert token in proc_h + proc


def test_wait_exit_zombie_reap_and_nonblocking_context_rules() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('src/kernel/proc/proc.cc')
    syscall_h = read_source('include/bigos/syscall.h')
    syscall = read_source('src/kernel/syscall/syscall.cc')

    for token in (
        'Zombie',
        'ReapPending',
        'WAIT_ECHILD = -10',
        'WAIT_EWOULDBLOCK = -11',
        'int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept;',
    ):
        assert token in proc_h

    assert 'mark_zombie_or_reap_pending(process)' in proc
    assert 'bigos::sched::wake_all(&g_process_wait_queue)' in proc
    assert 'if (!bigos::sched::can_block())' in proc
    assert 'bigos::sched::wait_queue_wait_until(&g_process_wait_queue' in proc
    assert 'mark_reap_pending(match);' in proc
    assert 'SYS_WAIT = 4' in syscall_h
    assert 'bigos::proc::wait_current((uint32_t)__frame->rdi, nullptr)' in syscall
