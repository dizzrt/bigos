from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_vfs_shell_defines_read_only_root_file_and_error_contracts() -> None:
    header = read_source('include/bigos/fs/vfs.h')
    source = read_source('src/kernel/fs/vfs.cc')

    assert 'namespace vfs' in header
    assert 'struct Vnode' in header
    assert 'struct File' in header
    assert 'struct FileOperations' in header
    assert 'OPEN_RDONLY = 0' in header
    assert 'OPEN_CREAT' in header and 'OPEN_TRUNC' in header
    assert 'Status::BadFileDescriptor' in source
    assert 'fs_to_vfs' in source
    assert 'g_root' in source


def test_vfs_init_mounts_exfat_without_publishing_partial_root() -> None:
    source = read_source('src/kernel/fs/vfs.cc')

    init_index = source.index('Status init() noexcept')
    publish_index = source.index('g_initialized = true;', init_index)

    assert 'ata_pio_primary_master_init(&ata);' in source
    assert 'find_exfat_partition(&ata.block, &partition)' in source
    assert 'mount_exfat(&ata.block, &partition, &mount)' in source
    assert 'g_ata = ata;' in source
    assert 'mount.device = &g_ata.block;' in source
    assert publish_index > source.index('g_mount = mount;', init_index)


def test_vfs_open_rejects_unsupported_paths_and_write_flags() -> None:
    source = read_source('src/kernel/fs/vfs.cc')

    assert 'readonly_flags' in source
    assert 'OPEN_WRONLY | bigos::vfs::OPEN_RDWR | bigos::vfs::OPEN_CREAT | bigos::vfs::OPEN_TRUNC' in source
    assert 'path_supported' in source
    assert "__path[0] != '/'" in source
    assert "start[0] == '.'" in source
    assert 'metadata.is_directory' in source
    assert 'bigos::fs::lookup(&g_mount, __path, &metadata)' in source


def test_vfs_read_clamps_eof_and_advances_offset_only_on_success() -> None:
    source = read_source('src/kernel/fs/vfs.cc')

    eof_index = source.index('__file->offset >= state->metadata.data_length')
    read_index = source.index('bigos::fs::read_file', eof_index)
    advance_index = source.index('__file->offset += result.bytes_read;', read_index)

    assert 'add_overflow(__file->offset, (uint64_t)__len, &end)' in source
    assert 'return fs_to_vfs(result.status);' in source
    assert eof_index < read_index < advance_index
    assert '*__bytes_read = result.bytes_read;' in source


def test_process_fd_table_lifecycle_and_helpers_are_bounded() -> None:
    header = read_source('include/bigos/proc.h')
    source = read_source('src/kernel/proc/proc.cc')

    assert 'constexpr uint32_t MAX_FDS = 16' in header
    assert 'FdEntry fd_table[MAX_FDS]' in header
    assert 'init_fd_table(__process);' in source
    assert 'install_fd_current' in source
    assert 'for (uint32_t i = 0; i < MAX_FDS; i++)' in source
    assert 'return -bigos::EMFILE;' in source
    assert 'read_fd_current' in source
    assert 'close_fd_current' in source
    assert 'close_all_fds(process);' in source


def test_exec_and_reap_apply_fd_close_rules_from_safe_context() -> None:
    source = read_source('src/kernel/proc/proc.cc')

    exec_index = source.index('exec_current_from_elf_image')
    close_exec_index = source.index('close_on_exec_fds(process);', exec_index)
    reap_index = source.index('void reap_pending_processes() noexcept')
    close_all_index = source.index('close_all_fds(process);', reap_index)
    stack_check_index = source.index('stack_is_active(process)', reap_index)
    root_check_index = source.index('process->address_space_root == (bigos::mm::read_cr3()', reap_index)

    assert exec_index < close_exec_index
    assert stack_check_index < root_check_index < close_all_index
    assert 'entry->close_on_exec' in source
    assert 'bigos::vfs::release(file);' in source


def test_fd_syscalls_copy_user_memory_and_guard_blocking_context() -> None:
    syscall = read_source('src/kernel/syscall/syscall.cc')
    header = read_source('include/bigos/syscall.h')
    proc = read_source('src/kernel/proc/proc.cc')
    vmem = read_source('src/mm/vmem.cc')

    assert 'SYS_OPEN = 5' in header
    assert 'SYS_READ = 6' in header
    assert 'SYS_CLOSE = 7' in header
    assert 'SYS_IO_MAX_LEN = 512' in header
    assert 'copy_user_path' in syscall
    assert 'if (!bigos::sched::can_block())' in syscall
    assert 'bigos::vfs::init()' in syscall
    assert 'bigos::vfs::open_absolute(path, __flags, &file)' in syscall
    assert 'bigos::proc::install_fd_current(file, false)' in syscall
    assert 'bigos::proc::read_fd_current' in syscall
    assert 'bigos::proc::copy_to_current_user_buffer' in syscall
    assert 'bigos::proc::close_fd_current' in syscall
    assert 'validate_user_io_buffer' in proc
    assert 'user_range_writable(process->address_space_root, __addr, __len)' in proc
    assert 'copy_to_user_root' in vmem
    assert '(*pte & page_attr::WRITABLE) == 0' in vmem


def test_existing_smokes_use_vfs_open_read_close_path() -> None:
    kernel = read_source('src/kernel/kernel.cc')

    fs_smoke = kernel[kernel.index('void fs_smoke() noexcept') : kernel.index('#endif', kernel.index('void fs_smoke() noexcept'))]
    user_elf = kernel[kernel.index('void user_elf_smoke_entry') : kernel.index('BIGOS_USER_ELF_LOAD_PASSED')]

    assert 'bigos::vfs::init()' in fs_smoke
    assert 'bigos::vfs::open_absolute(FS_SMOKE_PATH' in fs_smoke
    assert 'bigos::vfs::read(file' in fs_smoke
    assert 'bigos::vfs::release(file);' in fs_smoke
    assert 'bigos::vfs::open_absolute(bigos::proc::USER_ELF_SMOKE_PATH' in user_elf
    assert 'bigos::vfs::read(file, image' in user_elf
