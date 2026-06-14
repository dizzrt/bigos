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


def test_vfs_shell_defines_read_only_root_file_and_error_contracts() -> None:
    header = read_source('include/bigos/fs/vfs.h')
    source = read_source('kernel/core/fs/vfs.cc')

    assert 'namespace vfs' in header
    assert 'struct Vnode' in header
    assert 'struct File' in header
    assert 'struct FileOperations' in header
    assert 'OPEN_RDONLY = 0' in header
    assert 'OPEN_CREAT' in header and 'OPEN_TRUNC' in header
    assert 'Range = -34' in header
    assert 'Status::BadFileDescriptor' in source
    assert 'fs_to_vfs' in source
    assert 'g_root' in source


def test_vfs_init_mounts_exfat_without_publishing_partial_root() -> None:
    source = read_source('kernel/core/fs/vfs.cc')

    init_index = source.index('Status init() noexcept')
    publish_index = source.index('g_initialized = true;', init_index)

    assert 'ata_pio_primary_master_init(&ata);' in source
    assert 'find_exfat_partition(&ata.block, &partition)' in source
    assert 'mount_exfat(&ata.block, &partition, &mount)' in source
    assert 'g_ata = ata;' in source
    assert 'mount.device = &g_ata.block;' in source
    assert publish_index > source.index('g_mount = mount;', init_index)


def test_vfs_open_rejects_unsupported_paths_and_write_flags() -> None:
    source = read_source('kernel/core/fs/vfs.cc')

    # Writable opens are now routed to the bigfs backend; the read-only exFAT
    # backend rejects write/create flags with EROFS.
    assert 'OPEN_WRONLY | OPEN_RDWR | OPEN_CREAT | OPEN_TRUNC' in source
    assert 'return Status::ReadOnlyFs;' in source
    assert 'path_supported' in source
    assert "__path[0] != '/'" in source
    assert "start[0] == '.'" in source
    assert 'metadata.is_directory' in source
    assert 'bigos::fs::lookup(&g_mount, __path, &metadata)' in source


def test_vfs_read_clamps_eof_and_advances_offset_only_on_success() -> None:
    source = read_source('kernel/core/fs/vfs.cc')

    eof_index = source.index('__file->offset >= state->metadata.data_length')
    read_index = source.index('bigos::fs::read_file', eof_index)
    advance_index = source.index('__file->offset += result.bytes_read;', read_index)

    assert 'add_overflow(__file->offset, (uint64_t)__len, &end)' in source
    assert 'return fs_to_vfs(result.status);' in source
    assert eof_index < read_index < advance_index
    assert '*__bytes_read = result.bytes_read;' in source


def test_process_fd_table_lifecycle_and_helpers_are_growable() -> None:
    header = read_source('include/bigos/proc.h')
    source = read_source('kernel/core/proc/proc.cc')

    # The former fixed 16-fd inline array is replaced by a growable heap array
    # bounded by a configurable soft limit; lowest-free allocation and EMFILE
    # degradation are preserved.
    assert 'MAX_FDS_SOFT_LIMIT = 256' in header
    assert 'FdEntry *fd_table;' in header
    assert 'init_fd_table(__process);' in source
    assert 'install_fd_current' in source
    assert 'for (uint32_t i = 0; i < process->fd_capacity; i++)' in source
    assert 'grow_fd_table(process, fd + 1)' in source
    assert 'return -bigos::EMFILE;' in source
    assert 'read_fd_current' in source
    assert 'close_fd_current' in source
    assert 'close_all_fds(process);' in source
    assert 'free_fd_table(process);' in source


def test_exec_and_reap_apply_fd_close_rules_from_safe_context() -> None:
    source = read_source('kernel/core/proc/proc.cc')

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
    syscall = read_source('kernel/core/syscall/syscall.cc')
    header = read_source('include/bigos/syscall.h')
    proc = read_source('kernel/core/proc/proc.cc')
    vmem = read_source('kernel/mm/vmem.cc')

    assert 'SYS_OPEN = 5' in header
    assert 'SYS_READ = 6' in header
    assert 'SYS_CLOSE = 7' in header
    assert 'SYS_RENAME = 33' in header
    assert 'SYS_IO_MAX_LEN = 512' in header
    assert 'copy_user_path' in syscall
    assert 'if (!bigos::sched::can_block())' in syscall
    assert 'bigos::vfs::init()' in syscall
    assert 'bigos::vfs::open(path, bigos::proc::current_cwd(), __flags, (uint32_t)__mode, uid, gid, &file)' in syscall
    assert 'bigos::proc::install_fd_current(file, false)' in syscall
    assert 'bigos::proc::read_fd_current' in syscall
    assert 'bigos::proc::copy_to_current_user_buffer' in syscall
    assert 'bigos::proc::close_fd_current' in syscall
    assert 'sys_stat(__frame->rdi, __frame->rsi)' in syscall
    assert 'sys_fstat(__frame->rdi, __frame->rsi)' in syscall
    assert 'sys_rename(__frame->rdi, __frame->rsi)' in syscall
    assert 'bigos::vfs::rename(old_path, new_path, bigos::proc::current_cwd(), uid, gid)' in syscall
    assert 'sizeof(bigos::Metadata)' in syscall
    assert 'validate_user_io_buffer' in proc
    assert 'user_range_writable(process->address_space_root, __addr, __len)' in proc
    assert 'copy_to_user_root' in vmem
    assert '(*pte & page_attr::WRITABLE) == 0' in vmem
    assert 'return -bigos::ERANGE;' in syscall


def test_bounded_metadata_contract_is_vfs_backed() -> None:
    header = read_source('include/bigos/metadata.h')
    vfs_h = read_source('include/bigos/fs/vfs.h')
    vfs = read_source('kernel/core/fs/vfs.cc')

    assert 'struct Metadata' in header
    assert 'BIGOS_METADATA_TYPE_REGULAR = 1' in header
    assert 'BIGOS_METADATA_TYPE_DIRECTORY = 2' in header
    assert 'uint64_t object_id;' in header
    assert 'uint64_t reserved[4];' in header
    assert 'Status stat_absolute(const char *__path, bigos::Metadata *__out)' in vfs_h
    assert 'Status stat_path(const char *__path, const char *__cwd, bigos::Metadata *__out)' in vfs_h
    assert 'Status stat(File *__file, bigos::Metadata *__out)' in vfs_h
    assert 'Status rename(const char *__old_path, const char *__new_path, const char *__cwd' in vfs_h
    assert 'fill_metadata_defaults(__out)' in vfs
    assert '__out->object_id = 0;' in vfs
    assert 'bigos::fs::lookup(&g_mount, __path, &metadata)' in vfs
    assert 'bigos::bigfs::stat(__inode' in vfs
    assert 'return Status::Unsupported;' in vfs


def test_current_directory_resolution_contract_is_shared() -> None:
    header = read_source('include/bigos/proc.h')
    vfs_h = read_source('include/bigos/fs/vfs.h')
    vfs = read_source('kernel/core/fs/vfs.cc')
    proc = read_source('kernel/core/proc/proc.cc')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert 'char cwd[bigos::vfs::MAX_PATH_LEN + 1];' in header
    assert 'Status resolve_path(const char *__path, const char *__cwd' in vfs_h
    assert "root's parent remains root" in vfs_h
    assert "component[0] == '.' && component[1] == '.'" in vfs
    assert 'memcpy(child->cwd, parent->cwd, sizeof(child->cwd));' in proc
    assert 'init_cwd(__process);' in proc
    assert 'bigos::vfs::open(__path, process->cwd' in proc
    assert 'case SYS_CHDIR:' in syscall
    assert 'case SYS_GETCWD:' in syscall
    assert 'case SYS_RENAME:' in syscall


def test_existing_smokes_use_vfs_open_read_close_path() -> None:
    kernel = read_source('kernel/core/kernel.cc')

    fs_smoke = kernel[
        kernel.index('void fs_smoke() noexcept') : kernel.index('#endif', kernel.index('void fs_smoke() noexcept'))
    ]
    user_elf = kernel[kernel.index('void user_elf_smoke_entry') : kernel.index('BIGOS_USER_ELF_LOAD_PASSED')]

    assert 'bigos::vfs::init()' in fs_smoke
    assert 'bigos::vfs::open_absolute(FS_SMOKE_PATH' in fs_smoke
    assert 'bigos::vfs::read(file' in fs_smoke
    assert 'bigos::vfs::release(file);' in fs_smoke
    assert 'bigos::vfs::open_absolute(bigos::proc::USER_ELF_SMOKE_PATH' in user_elf
    assert 'bigos::vfs::read(file, image' in user_elf


def test_shell_prompts_only_for_default_console_stdio() -> None:
    shell = read_source('user/sh/sh.c')

    assert 'static int fd_has_installed_file(int fd)' in shell
    assert 'int dup_fd = dup(fd);' in shell
    assert 'return !fd_has_installed_file(0) && !fd_has_installed_file(1);' in shell
    assert 'int interactive = is_interactive_session();' in shell
    assert 'if (interactive)\n            write_all(1, prompt);' in shell
    assert 'read_line(line, sizeof(line), interactive)' in shell


def test_shell_line_editor_is_bounded_and_non_irq_userland_echo() -> None:
    shell = read_source('user/sh/sh.c')

    assert 'static int read_line(char *buf, int cap, int interactive)' in shell
    assert "if (ch == '\\b' || ch == 0x7f)" in shell
    assert 'len--;' in shell
    assert 'write_all(1, "\\b");' in shell
    assert 'write_all(1, "\\n");' in shell
    assert 'should_echo_input(ch)' in shell
    assert "while (n > 0 && ch != '\\n')" in shell
    assert 'return -1;' in shell


def test_shell_errors_and_builtin_redirection_use_fd_paths() -> None:
    shell = read_source('user/sh/sh.c')

    assert 'static void sh_error(const char *prefix, const char *detail)' in shell
    assert 'write_all(2, prefix);' in shell
    assert 'write_all(2, detail);' in shell
    assert 'static int run_builtin(int argc, char **argv, int out_fd, int last_status, int *status)' in shell
    assert 'int fd = out_fd >= 0 ? out_fd : 1;' in shell
    assert 'strcmp(argv[0], "cd") == 0' in shell
    assert 'chdir(argv[1])' in shell
    assert 'if (run_builtin(n, args, out_fd, last_status, &status))' in shell
    assert 'sh: line too long' in shell
    assert 'sh: command not found: ' in shell


def test_runtime_filesystem_error_mapping_and_user_visible_boundary() -> None:
    notes = read_source('openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    libc = read_source('user/libc/syscall.c')
    user_errno = read_source('user/libc/include/errno.h')
    shell = read_source('user/sh/sh.c')
    tools = '\n'.join(
        read_source(path)
        for path in (
            'user/bin/cat.c',
            'user/bin/ls.c',
            'user/bin/mkdir.c',
            'user/bin/rm.c',
            'user/bin/rename.c',
            'user/bin/stat.c',
        )
    )

    for errno_name in (
        'ENOENT',
        'EEXIST',
        'EBADF',
        'EACCES',
        'EROFS',
        'ENOSPC',
        'ENOMEM',
        'ERANGE',
        'EINVAL',
        'EFAULT',
        'ESPIPE',
        'ENOTDIR',
        'EISDIR',
        'EIO',
        'ENODEV',
        'EOPNOTSUPP',
        'EWOULDBLOCK',
    ):
        assert f'-{errno_name}' in notes
        assert errno_name in user_errno

    assert 'static int64_t vfs_status_to_syscall(bigos::vfs::Status __status) noexcept' in syscall
    assert 'return (int64_t)__status;' in syscall
    assert 'static long errno_translate(long ret)' in libc
    assert 'errno = (int)(-ret);' in libc
    assert 'const char *strerror(int errnum)' in read_source('user/libc/string.c')
    assert 'void perror(const char *s)' in read_source('user/libc/stdio.c')
    assert 'status_name(' not in libc
    assert 'status_name(' not in shell
    assert 'status_name(' not in tools
    assert 'errno=' in tools
    assert 'static void sh_errno_error(const char *prefix, const char *detail, int err)' in shell
    assert 'sh_errno_error("sh: cannot open ", argv[r + 1], errno)' in shell
    assert 'sh_errno_error("sh: cd failed: ", argv[1], errno)' in shell


def test_runtime_filesystem_blocking_guard_review_is_documented() -> None:
    notes = read_source('openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    for syscall_name in (
        'SYS_OPEN',
        'SYS_READ',
        'SYS_WRITE',
        'SYS_PIPE',
        'SYS_FSYNC',
        'SYS_MKDIR',
        'SYS_UNLINK',
        'SYS_RENAME',
        'SYS_READDIR',
        'SYS_STAT',
        'SYS_FSTAT',
        'SYS_CHDIR',
        'SYS_EXECVE',
    ):
        assert syscall_name in notes

    for function_name in (
        'static int64_t sys_open',
        'static int64_t sys_read',
        'static int64_t sys_write',
        'static int64_t sys_pipe',
        'static int64_t sys_fsync',
        'static int64_t sys_mkdir',
        'static int64_t sys_unlink',
        'static int64_t sys_rename',
        'static int64_t sys_readdir',
        'static int64_t sys_stat',
        'static int64_t sys_fstat',
        'static int64_t sys_chdir',
        'static int64_t sys_execve',
    ):
        start = syscall.index(function_name)
        next_function = syscall.find('static int64_t', start + len(function_name))
        if next_function < 0:
            next_function = len(syscall)
        body = syscall[start:next_function]
        assert 'if (!bigos::sched::can_block())' in body
        assert 'return -bigos::EWOULDBLOCK;' in body


def test_runtime_filesystem_backend_dispatch_and_open_ref_review_is_documented() -> None:
    notes = read_source('openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md')
    vfs = read_source('kernel/core/fs/vfs.cc')
    proc = read_source('kernel/core/proc/proc.cc')
    bigfs = read_source('kernel/core/fs/bigfs.cc')
    smoke = read_source('user/smoke/userland_smoke.c')

    assert 'Backend Dispatch And Open References' in notes
    for phrase in (
        'vfs::resolve_path(path, cwd, ...)',
        'bigfs::owns_path()',
        'Status::ReadOnlyFs',
        'vfs::retain()',
        'close_all_fds()',
        '-EEXIST',
    ):
        assert phrase in notes

    assert 'return open_absolute(resolved, __flags, __mode, __uid, __gid, __out_file);' in vfs
    assert 'return stat_absolute(resolved, __out);' in vfs
    assert 'return mkdir(resolved, __mode, __uid, __gid);' in vfs
    assert 'return unlink(resolved, __uid, __gid);' in vfs
    assert 'return rename(old_resolved, new_resolved, __uid, __gid);' in vfs
    assert 'if (!bigos::bigfs::owns_path(__old_path) || !bigos::bigfs::owns_path(__new_path))' in vfs
    assert 'return Status::ReadOnlyFs;' in vfs
    assert 'bigos::vfs::retain(entries[i].file);' in proc
    assert 'close_on_exec_fds(process);' in proc
    assert 'close_all_fds(process);' in proc
    assert 'maybe_free_unlinked_inode(target, &tnode);' in bigfs
    assert 'return Status::Exists;' in bigfs
    assert 'rename("/rw/runtime_rename_dst.txt", "/rw/runtime_rename_existing.txt") != -1 || errno != EEXIST' in smoke
    assert 'rename("/boot/user/init.elf", "/rw/runtime_rename_ro.txt") != -1 || errno != EROFS' in smoke
    assert 'rename("/rw/runtime_rename_dst.txt", "/boot/runtime_rename_dst.txt") != -1 || errno != EROFS' in smoke
    assert 'open("/rw/runtime_unlink.txt", O_RDONLY, 0) != -1 || errno != ENOENT' in smoke
    assert 'fstat(fd, &st) != 0 || st.st_size != 4' in smoke
    assert 'rename("rel_old.txt", "rel_new.txt") != 0' in smoke


def test_runtime_filesystem_metadata_contract_review_is_documented() -> None:
    notes = read_source('openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md')
    vfs = read_source('kernel/core/fs/vfs.cc')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    smoke = read_source('user/smoke/userland_smoke.c')
    stat_tool = read_source('user/bin/stat.c')

    assert 'Metadata Contract Review' in notes
    for phrase in (
        'path metadata',
        'fd metadata',
        'object_id',
        'stable inode identity',
        'Metadata metadata = {}',
        'partial/uninitialized metadata',
    ):
        assert phrase in notes

    assert 'fill_metadata_defaults(__out)' in vfs
    assert '__out->object_id = 0;' in vfs
    assert '__out->mode = (__metadata->is_directory ? (bigos::BIGOS_MODE_IFDIR | 0555) : (bigos::BIGOS_MODE_IFREG | 0444));' not in vfs
    assert '__out->mode = __metadata->is_directory ? (bigos::BIGOS_MODE_IFDIR | 0555) : (bigos::BIGOS_MODE_IFREG | 0444);' in vfs
    assert 'return fill_bigfs_metadata(state->inode, __out);' in vfs
    assert 'if (!bigos::proc::validate_user_io_buffer(__out, sizeof(bigos::Metadata)))' in syscall
    assert 'bigos::Metadata metadata = {};' in syscall
    assert 'copy_to_current_user_buffer(__out, &metadata, sizeof(metadata))' in syscall
    assert 'stat("/rw/runtime_rename_src.txt", &st) != -1 || errno != ENOENT' in smoke
    assert 'stat("/rw/runtime_rename_dst.txt", &st) != 0 || st.st_size != 11' in smoke
    assert 'stat("/rw/runtime_unlink.txt", &st) != -1 || errno != ENOENT' in smoke
    assert 'fstat(fd, &st) != 0 || st.st_size != 4' in smoke
    assert 'st.st_object_id != 0' in smoke
    assert 'object=' in stat_tool
