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


def test_user_elf_smoke_is_default_off_and_separate_from_embedded_smoke() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    proc_h = read_source('include/bigos/proc.h')

    option_index = xmake.index('option("user_elf_smoke")')
    default_index = xmake.index('set_default(false)', option_index)
    define_index = xmake.index('add_defines("BIGOS_USER_ELF_SMOKE")')
    assert option_index < default_index < define_index
    assert 'add_defines("BIGOS_USER_PROCESS")' in xmake
    assert 'add_files("$(projectdir)/kernel/core/proc/**.cc")' in xmake
    assert 'if has_config("user_program_smoke") then' in xmake
    assert 'add_defines("BIGOS_USER_PROGRAM_SMOKE")' in xmake
    assert 'BIGOS_USER_ELF_LOAD_PASSED' in kernel
    assert 'bigos::proc::USER_ELF_SMOKE_PATH' in kernel
    assert 'bigos::vfs::open_absolute(bigos::proc::USER_ELF_SMOKE_PATH' in kernel
    assert 'bigos::kmalloc((size_t)file_size)' in kernel
    assert 'constexpr const char *USER_ELF_SMOKE_PATH = "/boot/user/init.elf";' in proc_h


def test_user_elf_header_program_header_and_address_bounds_are_checked() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    for token in (
        'ELF_MAGIC',
        'ehdr->ident[4] != 2',
        'ehdr->ident[5] != 1',
        'ELF_MACHINE_X86_64',
        'ELF_TYPE_EXEC',
        'ehdr->ehsize != sizeof(Elf64Header)',
        'ehdr->phentsize != sizeof(Elf64ProgramHeader)',
        '__builtin_mul_overflow',
        'ph_table_end > __image_len',
        'phdr->filesz > phdr->memsz',
        'file_end > __image_len',
        'USER_LOW_HALF_LIMIT',
        'USER_STACK_TOP - bigos::proc::USER_STACK_PAGES * PAGE_SIZE',
    ):
        assert token in proc


def test_user_elf_rejects_unsupported_segments_overlap_wx_and_bad_entry() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'phdr->type == PT_DYNAMIC || phdr->type == PT_INTERP || phdr->type == PT_TLS' in proc
    assert 'is_writable_executable(phdr->flags)' in proc
    assert 'return bigos::proc::UserElfLoadError::SegmentOverlap;' in proc
    assert 'return bigos::proc::UserElfLoadError::EntryNotExecutable;' in proc
    assert 'track_page(tracked_pages, &tracked_page_count, page, attr)' in proc


def test_user_elf_maps_pages_with_explicit_attrs_and_zero_fills_bss() -> None:
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'segment_attr(uint32_t __flags)' in proc
    assert 'attr |= bigos::mm::page_attr::WRITABLE | bigos::mm::page_attr::NO_EXECUTE;' in proc
    assert 'attr |= bigos::mm::page_attr::NO_EXECUTE;' in proc
    assert 'memset(direct, 0, PAGE_SIZE);' in proc
    assert 'memcpy((uint8_t *)direct + page_offset, __image + file_offset, copy_end - copy_vaddr);' in proc
    # Stage 19: ELF segment pages live only in the process's own root; mapping
    # them into the active root too would pollute/collide with whatever root is
    # current during an execve from another process, so also_map_active_root=false.
    assert 'map_user_page_for_process(__process, page, phys, __segment.attr, false)' in proc
    assert 'bool map_user_page_for_process(' in proc
    assert 'internal_vma_range_allowed(&__process->vmas, __vaddr, PAGE_SIZE, bigos::proc::VmaPermission::Read)' in proc
    assert '!internal_vma_attr_allowed(vma, __attr)' in proc
    assert 'bigos::mm::map_page_in_root(__process->address_space_root, __vaddr, __phys, __attr)' in proc
    assert 'map_page_in_root(' in proc and 'bigos::mm::page_attr::USER_DATA' in proc


def test_user_elf_failure_paths_release_owned_resources_and_keep_reaper_boundary() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    sched = read_source('kernel/core/sched/sched.cc')

    assert 'goto fail;' in proc
    assert 'free_user_frame(phys);' in proc
    assert 'teardown_user_address_space(__process->address_space_root)' in proc
    assert 'bigos::free_pages(__process->kernel_stack_base);' in proc
    assert 'BIGOS_USER_REAP_DEFERRED active-root' in proc
    assert 'BIGOS_USER_REAP_DEFERRED active-stack' in proc
    assert 'bigos::proc::reap_pending_processes();' in sched


def test_user_elf_exec_prepare_commit_and_argv_envp_bounds_are_source_checked() -> None:
    proc_h = read_source('include/bigos/proc.h')
    proc = read_source('kernel/core/proc/proc.cc')
    kernel = read_source('kernel/core/kernel.cc')

    for token in (
        'constexpr uint32_t EXEC_MAX_ARGC = 8;',
        'constexpr uint32_t EXEC_MAX_ENVC = 8;',
        'constexpr uint64_t EXEC_MAX_STRING_BYTES = 256;',
        'struct ExecArgs',
        'exec_current_from_elf_image',
        'EXEC_FAILURE_STATUS',
    ):
        assert token in proc_h

    assert 'copy_exec_args_to_stack' in proc
    assert 'args->argc > bigos::proc::EXEC_MAX_ARGC' in proc
    assert 'args->envc > bigos::proc::EXEC_MAX_ENVC' in proc
    assert 'bounded_strlen(args->argv[i], bigos::proc::EXEC_MAX_STRING_BYTES)' in proc
    assert 'create_elf_user_process(prepared, __image, __image_len, __args)' in proc
    assert 'unpublish_process(prepared)' in proc
    assert 'init_runtime_layout(__process' in proc
    assert 'process->runtime_layout = prepared->runtime_layout;' in proc
    assert 'process->exit_code = EXEC_FAILURE_STATUS;' in proc
    assert 'bigos::proc::ExecArgs args = {argv, 1, nullptr, 0};' in kernel
