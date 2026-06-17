from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHANGE = ROOT / 'openspec/changes/mature-runtime-filesystem-semantics'


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def read_change(relative: str) -> str:
    return (CHANGE / relative).read_text(encoding='utf-8')


def test_runtime_filesystem_maturity_change_artifacts_record_runtime_boundaries_and_backend_matrix() -> None:
    proposal = read_change('proposal.md')
    design = read_change('design.md')
    runtime_spec = read_change('specs/runtime-filesystem-maturity/spec.md')
    writable_spec = read_change('specs/writable-filesystem/spec.md')
    fd_spec = read_change('specs/fd-vfs-shell/spec.md')

    for text in (proposal, design, runtime_spec, writable_spec):
        assert 'RAM-backed' in text
        assert 'current-runtime' in text or '当前运行期' in text or '运行期一致性' in text
        assert 'cross-reboot persistence' in text or '跨重启持久化' in text

    assert 'read-only exFAT' in proposal
    assert 'open, read, write, lseek, fsync, mkdir, unlink, restricted regular-file rename, stat, fstat' in fd_spec
    assert '`/rw` directory slot order' in fd_spec
    assert '稳定后端顺序' in design


def test_runtime_filesystem_maturity_vfs_errno_and_zero_length_read_contracts() -> None:
    kernel_errno = read_source('include/bigos/errno.h')
    user_errno = read_source('user/libc/include/errno.h')
    user_string = read_source('user/libc/string.c')
    vfs_h = read_source('include/bigos/fs/vfs.h')
    vfs = read_source('kernel/core/fs/vfs.cc')
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'ENOTEMPTY = 39;' in kernel_errno
    assert '#define ENOTEMPTY   39' in user_errno
    assert 'case ENOTEMPTY:' in user_string
    assert 'NotEmpty = -39' in vfs_h
    assert 'case bigos::bigfs::Status::NotEmpty:' in vfs
    assert 'return bigos::vfs::Status::NotEmpty;' in vfs
    assert 'case Status::NotEmpty:' in vfs

    read_start = bigfs.index('Status read(uint32_t __inode')
    read_body = bigfs[read_start : bigfs.index('Status write(uint32_t __inode', read_start)]
    assert 'if (__len == 0)\n            return Status::Success;' in read_body
    assert 'if (__dst == nullptr)\n            return Status::Invalid;' in read_body


def test_runtime_filesystem_maturity_directory_growth_rolls_back_before_publication() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    reserve_start = bigfs.index('Status dir_reserve_slot')
    reserve_body = bigfs[reserve_start : bigfs.index('void dirent_set', reserve_start)]
    add_start = bigfs.index('Status dir_add_entry')
    add_body = bigfs[add_start : bigfs.index('bool dir_remove_entry', add_start)]

    for body in (reserve_body, add_body):
        assert 'bigos::bcache::BufferBlock *block = bigos::bcache::get(&g_device, new_block);' in body
        assert 'DiskInode committed = *__dir;' in body
        assert 'committed.direct[i] = new_block;' in body
        assert 'committed.size += BLOCK_SIZE;' in body
        assert 'if (!store_inode(__dir_inode, &committed))' in body
        assert 'free_data_block(new_block);' in body
        assert '*__dir = committed;' in body

    assert bigfs.index('bigos::bcache::get(&g_device, new_block)', reserve_start) < bigfs.index(
        'if (!store_inode(__dir_inode, &committed))', reserve_start
    )
    assert bigfs.index('bigos::bcache::get(&g_device, new_block)', add_start) < bigfs.index(
        'if (!store_inode(__dir_inode, &committed))', add_start
    )


def test_runtime_filesystem_maturity_truncate_commits_metadata_before_freeing_old_blocks() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    trunc_start = bigfs.index('if ((__flags & bigos::vfs::OPEN_TRUNC) != 0)')
    trunc_body = bigfs[trunc_start : bigfs.index('g_open_refs[inode_num]++;', trunc_start)]

    assert 'DiskInode truncated = file;' in trunc_body
    assert 'truncated.direct[i] = 0;' in trunc_body
    assert 'truncated.size = 0;' in trunc_body
    assert 'if (!store_inode(inode_num, &truncated))' in trunc_body
    assert 'free_data_block(file.direct[i]);' in trunc_body
    assert trunc_body.index('if (!store_inode(inode_num, &truncated))') < trunc_body.index(
        'free_data_block(file.direct[i]);'
    )


def test_runtime_filesystem_maturity_cross_directory_rename_pins_source_before_destination_growth() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    rename_start = bigfs.index('Status rename(const char *__old_abs_path')
    cross_dir_start = bigfs.index('DirSlot src = {};', rename_start)
    rename_tail = bigfs[cross_dir_start : bigfs.index('Status readdir(uint32_t __inode', cross_dir_start)]

    assert 'if (!dir_find_slot(&old_dir, old_leaf, &src))' in rename_tail
    assert 'status = dir_reserve_slot(new_parent, &new_dir, &dst);' in rename_tail
    assert 'bigos::bcache::put(src.block);' in rename_tail
    assert rename_tail.index('if (!dir_find_slot(&old_dir, old_leaf, &src))') < rename_tail.index(
        'status = dir_reserve_slot(new_parent, &new_dir, &dst);'
    )
    assert rename_tail.index('dirent_set(dst.entry, new_leaf, target);') < rename_tail.index(
        'src.entry->inode_plus_one = 0;'
    )


def test_runtime_filesystem_maturity_user_buffer_and_fd_validation_contracts() -> None:
    proc = read_source('kernel/core/proc/proc.cc')
    syscall = read_source('kernel/core/syscall/syscall.cc')

    assert '__len > bigos::sys::SYS_IO_MAX_LEN' in proc
    assert 'validate_user_io_buffer(__out_fds, sizeof(uint32_t) * 2)' in syscall
    assert 'validate_user_io_buffer(__entries, bytes)' in syscall
    assert 'copy_to_current_user_buffer(__out, &metadata, sizeof(metadata))' in syscall


def test_runtime_filesystem_maturity_dedicated_filesystem_maturity_smoke_is_default_off() -> None:
    options = read_source('xmake/options.lua')
    kernel = read_source('xmake/kernel.lua')
    package = read_source('xmake/user_package.lua')
    smoke = read_source('user/smoke/userland_smoke.c')
    boot_debug = read_source('tools/boot_debug.py')
    docs_en = read_source('docs/en/arch/runtime-smoke-validation.md')
    docs_zh = read_source('docs/zh/arch/runtime-smoke-validation.md')

    option_start = options.index('option("filesystem_maturity_smoke")')
    assert 'set_default(false)' in options[option_start : option_start + 200]
    assert 'BIGOS_FILESYSTEM_MATURITY_SMOKE' in kernel
    assert 'has_config("filesystem_maturity_smoke")' in package
    assert 'BIGOS_FILESYSTEM_MATURITY_PASSED' in smoke
    assert "'filesystem_maturity_smoke'" in boot_debug
    assert "case_id='filesystem-maturity'" in boot_debug
    assert "expected_marker='BIGOS_FILESYSTEM_MATURITY_PASSED'" in boot_debug
    assert "boot_mode='legacy'" in boot_debug
    assert '`filesystem-maturity`' in docs_en
    assert '`filesystem-maturity`' in docs_zh


def test_runtime_filesystem_maturity_runtime_smoke_exercises_success_and_failure_paths() -> None:
    smoke = read_source('user/smoke/userland_smoke.c')

    for snippet in (
        'stat("/boot/user/init.elf", &st)',
        'open("/rw/runtime_file.txt", O_RDWR | O_CREAT | O_TRUNC, 0644)',
        'fsync(fd)',
        'bigos_readdir(dirfd, entries, BIGOS_DIRENT_MAX_BATCH)',
        'rename("/rw/runtime_rename_src.txt", "/rw/runtime_rename_dst.txt")',
        'unlink("/rw/runtime_unlink.txt")',
        'open("/boot/user/init.elf", O_WRONLY, 0)',
        'bigos_readdir(fd, entries, 1) != -1 || errno != ENOTDIR',
        'read(fd, (void *)1, 1) != -1 || errno != EFAULT',
        'stat("/rw/runtime_file.txt", (struct stat *)1) != -1 || errno != EFAULT',
        'fstat(250, &st)',
        'write(fd, "x", 1) != -1 || errno != ENOSPC',
        'bigos_readdir(dirfd, entries, BIGOS_DIRENT_MAX_BATCH + 1) != -1 || errno != ERANGE',
    ):
        assert snippet in smoke

    assert 'rename("rel_old.txt", "rel_new.txt")' in smoke
    assert 'getcwd(cwd, sizeof(cwd))' in smoke
