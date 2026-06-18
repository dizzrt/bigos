from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHANGE = ROOT / 'openspec/changes/archive/2026-06-18-add-stable-file-growth'


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def read_change(relative: str) -> str:
    return (CHANGE / relative).read_text(encoding='utf-8')


def test_stable_file_growth_change_artifacts_keep_bounded_clean_sync_scope() -> None:
    proposal = read_change('proposal.md')
    design = read_change('design.md')
    spec = read_change('specs/stable-file-growth/spec.md')

    for text in (proposal, design, spec):
        assert 'zero' in text or '零' in text
        assert 'truncate' in text
        assert 'clean reboot' in text or 'clean-sync' in text
    assert '不实现 journal' in design
    assert '不承诺完整 POSIX' in design


def test_bigfs_growth_and_truncate_stage_state_before_publication() -> None:
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    write_start = bigfs.index('Status write(uint32_t __inode')
    write_body = bigfs[write_start : bigfs.index('Status truncate(uint32_t __inode', write_start)]
    assert 'bitmap_count_free(DATA_BITMAP_BLOCK' in write_body
    assert 'reserve_data_block(&new_block)' in write_body
    assert 'rollback_allocated_blocks(allocated, allocated_count);' in write_body
    assert 'memset(g_write_staged[block_count], 0, BLOCK_SIZE);' in write_body
    assert 'bigos::bcache::BufferBlock *inode_block = bigos::bcache::get(&g_device, inode_block_no);' in write_body
    assert write_body.index('inode_block == nullptr') < write_body.index('memcpy(pinned[i]->data, g_write_staged[i], BLOCK_SIZE);')
    assert write_body.index('memcpy(pinned[i]->data, g_write_staged[i], BLOCK_SIZE);') < write_body.index(
        'memcpy(inode_block->data + inode_off, &committed, sizeof(DiskInode));'
    )

    truncate_body = bigfs[bigfs.index('Status truncate(uint32_t __inode') : bigfs.index('Status mkdir(')]
    assert 'if (__length > MAX_FILE_SIZE)' in truncate_body
    assert 'if (file.type != INODE_REGULAR)' in truncate_body
    assert 'check_access(__uid, __gid, &file, bigos::cred::Access::Write)' in truncate_body
    assert 'committed.size = __length;' in truncate_body
    assert 'memset(tail_staged + (__length % BLOCK_SIZE), 0' in truncate_body
    assert truncate_body.index('if (!store_inode(__inode, &committed))') < truncate_body.index(
        'free_data_block(file.direct[i]);'
    )


def test_vfs_syscall_and_libc_expose_only_bounded_ftruncate() -> None:
    vfs_h = read_source('include/bigos/fs/vfs.h')
    syscall_h = read_source('include/bigos/syscall.h')
    syscall_c = read_source('kernel/core/syscall/syscall.cc')
    user_sys = read_source('user/libc/syscall.c')
    unistd_h = read_source('user/libc/include/unistd.h')

    assert 'TruncateOp truncate;' in vfs_h
    assert 'Status truncate(File *__file, uint64_t __length)' in vfs_h
    assert 'SYS_FTRUNCATE = 39' in syscall_h
    assert 'static int64_t sys_ftruncate(uint64_t __fd, uint64_t __length)' in syscall_c
    assert 'bigos::proc::truncate_fd_current((uint32_t)__fd, __length)' in syscall_c
    assert 'int ftruncate(int fd, off_t length)' in unistd_h
    assert 'int truncate(const char *path, off_t length)' in unistd_h
    assert 'syscall2(SYS_FTRUNCATE' in user_sys
    assert 'int fd = open(path, O_WRONLY, 0);' in user_sys


def test_runtime_validation_covers_growth_truncate_reuse_and_persistence() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    smoke = read_source('user/smoke/userland_smoke.c')
    docs_en = read_source('docs/en/arch/writable-fs-page-cache-pipe.md')
    docs_zh = read_source('docs/zh/arch/writable-fs-page-cache-pipe.md')

    for token in (
        'wfs_check_stable_growth',
        'pfs_write_stable_growth_state',
        'pfs_verify_stable_growth_state',
        'BIGOS_WRITABLE_FS_FAILED stable-growth',
        'BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_FAILED stable-growth',
    ):
        assert token in kernel
    for token in (
        'runtime-growth-gap-zero',
        'runtime-ftruncate-extend-zero',
        'runtime-ftruncate-enospc-size',
        'runtime-reuse-zero',
        'runtime-truncate-rofs',
    ):
        assert token in smoke
    assert 'seek past EOF' in docs_en
    assert 'seek 越过' in docs_zh
