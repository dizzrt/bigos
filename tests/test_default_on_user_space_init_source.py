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


def test_launch_init_is_declared_with_neutral_init_path_constant() -> None:
    proc_h = read_source('include/bigos/proc.h')

    assert 'constexpr const char *INIT_ELF_PATH = "/boot/user/init.elf";' in proc_h
    # USER_ELF_SMOKE_PATH stays unchanged (decision 3).
    assert 'constexpr const char *USER_ELF_SMOKE_PATH = "/boot/user/init.elf";' in proc_h
    assert 'void launch_init(void *) noexcept;' in proc_h


def test_launch_init_reuses_vfs_elf_load_chain_and_emits_enter_marker() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'void launch_init(void *) noexcept {' in proc
    assert 'bigos::vfs::init();' in proc
    assert 'bigos::vfs::open_absolute(INIT_ELF_PATH' in proc
    assert 'bigos::kmalloc((size_t)file_size)' in proc
    # init process object is now heap-allocated (replaces the static singleton).
    assert 'Process *init_process = alloc_process_object();' in proc
    assert 'create_elf_user_process(init_process, image, file_size, &args)' in proc
    assert 'bigos::serial_puts("BIGOS_INIT_ENTER\\n");' in proc
    assert 'run_user_process(init_process);' in proc


def test_launch_init_degrades_deterministically_via_panic() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'BIGOS_INIT_LOAD_FAILED ' in proc
    assert 'USER_ELF_MAX_FILE_BYTES' in proc
    assert 'bigos::kpanic(bigos::PanicCode::Generic, "launch_init");' in proc


def test_exec_teardown_uses_stable_kernel_address_space_root() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'uint64_t g_kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;' in proc
    assert 'g_kernel_address_space_root = bigos::arch::vm_user::active_address_space_root();' in proc

    activate_body = proc[proc.index('void activate_process_kernel_address_space(') : proc.index('bool path_equal(')]
    assert '__process->kernel_address_space_root != __process->address_space_root' in activate_body
    assert 'bigos::arch::vm_user::activate_kernel_address_space(g_kernel_address_space_root);' in activate_body

    exec_body = proc[proc.index('UserElfLoadError exec_current_from_elf_image(') : proc.index('void run_user_process(')]
    assert 'process->kernel_address_space_root = g_kernel_address_space_root;' in exec_body
    switch_index = exec_body.index('bigos::arch::vm_user::activate_kernel_address_space(g_kernel_address_space_root);')
    teardown_index = exec_body.index('bigos::mm::teardown_user_address_space(old_root)')
    assert switch_index < teardown_index


def test_init_normal_exit_emits_exit_marker_then_idle_not_panic() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    # BIGOS_INIT_EXIT is emitted from the existing exit path and falls through to
    # the deferred reaper / idle scheduling, not a panic.
    assert 'if (process == g_init_process)' in proc
    assert 'bigos::serial_puts("BIGOS_INIT_EXIT\\n");' in proc
    exit_index = proc.index('BIGOS_INIT_EXIT')
    thread_exit_index = proc.index('bigos::sched::thread_exit();', exit_index)
    assert exit_index < thread_exit_index


def smoke_guard_depth_before(source: str, needle: str) -> int:
    """Net depth of SMOKE-related #ifdef guards open at the first `needle`.

    Walks a stack of all preprocessor conditionals so balanced non-smoke guards
    (such as file-level include guards) are ignored, and only unbalanced
    SMOKE-tagged guards are counted.
    """
    index = source.index(needle)
    stack: list[bool] = []
    for line in source[:index].splitlines():
        stripped = line.strip()
        if re.match(r'#if(def|ndef)?\b', stripped):
            stack.append('SMOKE' in stripped)
        elif stripped.startswith('#endif') and stack:
            stack.pop()
    return sum(1 for is_smoke in stack if is_smoke)


def test_launch_init_and_call_site_have_no_ifdef_guard() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    proc_h = read_source('include/bigos/proc.h')
    kernel = read_source('kernel/core/kernel.cc')

    # The launch_init call site in kernel() must not be wrapped by any smoke
    # #ifdef block.
    call = 'bigos::sched::create_kernel_thread(&bigos::proc::launch_init, nullptr)'
    assert call in kernel
    assert smoke_guard_depth_before(kernel, call) == 0

    # The launch_init definition and declaration must not be guarded by a smoke
    # #ifdef either (file-level include guards are balanced and ignored).
    assert smoke_guard_depth_before(proc, 'void launch_init(void *) noexcept {') == 0
    assert smoke_guard_depth_before(proc_h, 'void launch_init(void *) noexcept;') == 0


def test_user_init_elf_target_is_default_on() -> None:
    xmake = read_source('xmake.lua')

    target_index = xmake.index('target("user-init-elf")')
    default_index = xmake.index('set_default(true)', target_index)
    build_index = xmake.index('on_build', target_index)
    assert target_index < default_index < build_index
    # The target must no longer be gated on user_elf_smoke.
    segment = xmake[target_index:build_index]
    assert 'has_config("user_elf_smoke")' not in segment


def test_existing_user_smoke_switches_and_markers_are_preserved() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'if has_config("user_program_smoke") then' in xmake
    assert 'add_defines("BIGOS_USER_PROGRAM_SMOKE")' in xmake
    assert 'if has_config("user_elf_smoke") then' in xmake
    assert 'add_defines("BIGOS_USER_ELF_SMOKE")' in xmake
    assert '#ifdef BIGOS_USER_ELF_SMOKE' in kernel
    assert '#ifdef BIGOS_USER_PROGRAM_SMOKE' in kernel
