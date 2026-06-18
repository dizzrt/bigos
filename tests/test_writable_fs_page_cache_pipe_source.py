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


def test_syscall_numbers_appended_after_sigreturn() -> None:
    header = read_source('include/bigos/syscall.h')

    # Existing numbers stay fixed; the new ones are appended contiguously.
    assert 'SYS_SIGRETURN = 19' in header
    assert 'SYS_LSEEK = 20' in header
    assert 'SYS_PIPE = 21' in header
    assert 'SYS_DUP = 22' in header
    assert 'SYS_DUP2 = 23' in header
    assert 'SYS_FSYNC = 24' in header
    assert 'SYS_MKDIR = 25' in header
    assert 'SYS_UNLINK = 26' in header
    assert 'SYS_EXECVE = 27' in header
    assert 'SYS_READDIR = 28' in header
    assert 'SYS_STAT = 29' in header
    assert 'SYS_FSTAT = 30' in header
    assert 'SYS_CHDIR = 31' in header
    assert 'SYS_GETCWD = 32' in header
    assert 'SYS_RENAME = 33' in header
    assert 'SYS_RMDIR = 38' in header
    assert 'SYS_FTRUNCATE = 39' in header
    assert 'SYS_SYNC = 40' in header
    # Frozen ABI anchors must not move.
    assert 'SYS_OPEN = 5' in header
    assert 'SYS_WRITE = 2' in header


def test_errno_single_source_adds_new_codes() -> None:
    header = read_source('include/bigos/errno.h')

    for name, value in (
        ('EIO', 5),
        ('EACCES', 13),
        ('EEXIST', 17),
        ('ENODEV', 19),
        ('EISDIR', 21),
        ('ENOSPC', 28),
        ('ESPIPE', 29),
        ('EROFS', 30),
        ('EPIPE', 32),
        ('ERANGE', 34),
        ('ENOTEMPTY', 39),
        ('EOPNOTSUPP', 95),
    ):
        assert f'{name} = {value};' in header


def test_block_device_exposes_write_entry_point() -> None:
    header = read_source('include/drivers/block/block_device.h')
    source = read_source('kernel/drivers/block/block_device.cc')
    ata = read_source('kernel/drivers/block/ata_pio.cc')

    assert 'WriteSectorsFn write_impl' in header
    assert 'BlockStatus write_sectors(' in header
    # write_sectors validates a read-only (null write_impl) device before any IO.
    assert 'if (__device->write_impl == nullptr)' in source
    assert 'return BlockStatus::Unsupported;' in source
    # The ATA backend implements LBA48 write + flush and wires write_impl.
    assert 'ATA_CMD_WRITE_SECTORS_EXT' in ata
    assert 'ATA_CMD_FLUSH_CACHE_EXT' in ata
    assert '__device->block.write_impl = ata_write_impl;' in ata


def test_buffer_cache_write_back_and_eviction_contract() -> None:
    header = read_source('include/bigos/fs/bcache.h')
    source = read_source('kernel/core/fs/bcache.cc')

    assert 'struct BufferBlock' in header
    assert 'BufferBlock *get(' in header
    assert 'Status sync(' in header
    assert 'Status sync_device(driver::block::BlockDevice *__dev) noexcept;' in header
    assert 'Status sync_all(' in header
    assert 'persistent success paths should use' in header
    # Eviction prefers an unreferenced clean block, writes back a dirty victim.
    assert 'slot->ref_count != 0 || slot->dirty' in source
    assert 'write_back(dirty_lru)' in source
    # No evictable slot -> deterministic nullptr (caller maps to -ENOMEM).
    assert 'return nullptr;   // every slot is pinned' in source
    # Device write failure keeps the block dirty (no data loss).
    assert 'block::write_sectors(' in source
    assert '!bigos::sched::can_block() || !bigos::sched::preemption_enabled()' in source
    assert 'Status sync_device(driver::block::BlockDevice *__dev) noexcept' in source
    assert 'slot->dev != __dev || !slot->dirty' in source


def test_writable_fs_uses_cache_and_may_access_enforcement() -> None:
    header = read_source('include/bigos/fs/bigfs.h')
    source = read_source('kernel/core/fs/bigfs.cc')

    assert 'MOUNT_PREFIX = "/rw"' in header
    # All metadata/data goes through the block buffer cache.
    assert 'bigos::bcache::get(&g_device' in source
    assert 'bigos::bcache::mark_dirty' in source
    # cred::permits is the actual access enforcement point on the writable path.
    assert 'bigos::cred::permits(' in source
    assert 'Status::AccessDenied' in source
    # Deterministic failure codes.
    for status in ('Status::NoSpace', 'Status::Exists', 'Status::NotFound', 'Status::IsDirectory'):
        assert status in source
    # Failed create/mkdir publication clears newly allocated inode metadata.
    assert 'void discard_new_inode(uint32_t __inode) noexcept' in source
    assert 'DiskInode empty = {};' in source
    assert 'discard_new_inode(new_inode);' in source
    # Writes preflight capacity before copying data, so ENOSPC is not a partial success.
    assert 'Status bitmap_count_free(uint32_t __bitmap_block' in source
    assert 'Status reserve_data_block(uint32_t *__out_block) noexcept' in source
    assert 'if (__offset >= MAX_FILE_SIZE || (uint64_t)__len > MAX_FILE_SIZE - __offset)' in source
    assert 'if (free_blocks < missing_blocks)' in source
    assert 'rollback_allocated_blocks(allocated, allocated_count);' in source
    assert 'uint8_t g_write_staged[DIRECT_BLOCKS][BLOCK_SIZE] = {};' in source
    assert 'put_pinned_blocks(pinned, block_count);' in source
    assert 'memcpy(pinned[i]->data, g_write_staged[i], BLOCK_SIZE);' in source
    assert 'Status truncate(uint32_t __inode, uint64_t __length' in source
    assert 'if (__length > MAX_FILE_SIZE)' in source
    assert 'memset(tail_staged + (__length % BLOCK_SIZE), 0' in source
    assert 'for (uint32_t i = keep_blocks; i < DIRECT_BLOCKS; i++)' in source


def test_vfs_file_operations_gain_write_and_lseek() -> None:
    header = read_source('include/bigos/fs/vfs.h')
    source = read_source('kernel/core/fs/vfs.cc')

    assert 'WriteOp write;' in header
    assert 'LseekOp lseek;' in header
    assert 'TruncateOp truncate;' in header
    assert 'ReaddirOp readdir;' in header
    assert 'bool writable;' in header
    assert 'ReadOnlyFs' in header
    # Read-only exFAT backend leaves write/lseek null and is rejected with EROFS.
    assert '{&exfat_read, &exfat_close, nullptr, nullptr, nullptr, &exfat_readdir}' in source
    assert 'return Status::ReadOnlyFs;' in source
    assert 'Status sync_writable_backend() noexcept' in header
    assert 'bigos::bigfs::fsync()' in source
    # Writable open captures caller identity/mode for bigfs.
    assert 'bigos::bigfs::open(__path, __flags, __mode, __uid, __gid' in source
    assert '&bigfs_truncate' in source
    assert 'Status truncate(File *__file, uint64_t __length)' in source


def test_syscall_dispatch_routes_new_calls_and_guards_blocking() -> None:
    source = read_source('kernel/core/syscall/syscall.cc')

    for branch in (
        'case SYS_LSEEK:',
        'case SYS_PIPE:',
        'case SYS_DUP:',
        'case SYS_DUP2:',
        'case SYS_FSYNC:',
        'case SYS_SYNC:',
        'case SYS_MKDIR:',
        'case SYS_UNLINK:',
        'case SYS_READDIR:',
        'case SYS_STAT:',
        'case SYS_FSTAT:',
        'case SYS_CHDIR:',
        'case SYS_GETCWD:',
        'case SYS_FTRUNCATE:',
    ):
        assert branch in source
    # Allocation / blocking syscalls check the scheduler blocking guard.
    assert 'if (!bigos::sched::can_block())' in source
    # SYS_OPEN extended with mode + identity, SYS_WRITE keeps console fast path.
    assert 'sys_open(__frame->rdi, __frame->rsi, __frame->rdx)' in source
    assert 'sys_ftruncate(__frame->rdi, __frame->rsi)' in source
    assert 'static int64_t sys_sync()' in source
    assert 'bigos::vfs::sync_writable_backend()' in source
    assert 'BIGOS_USER_WRITE_SYSCALL' in source


def test_runtime_directory_enum_and_unlink_lifetime_contracts() -> None:
    bigfs_h = read_source('include/bigos/fs/bigfs.h')
    bigfs = read_source('kernel/core/fs/bigfs.cc')
    vfs = read_source('kernel/core/fs/vfs.cc')
    proc = read_source('kernel/core/proc/proc.cc')
    smoke = read_source('user/smoke/userland_smoke.c')

    assert 'struct DirectoryEntry' in bigfs_h
    assert 'Status readdir(' in bigfs_h
    assert 'Status rename(' in bigfs_h
    assert 'Status::Range' in vfs
    assert 'uint32_t g_open_refs[INODE_COUNT]' in bigfs
    assert 'void close_inode(uint32_t __inode)' in bigfs
    assert 'maybe_free_unlinked_inode' in bigfs
    assert 'bigos::bigfs::rename(' in vfs
    assert 'rename("/rw/runtime_rename_src.txt", "/rw/runtime_rename_dst.txt")' in smoke
    assert 'rename("/rw/runtime_rename_dst.txt", "/rw/runtime_rename_existing.txt")' in smoke
    assert "('sh', 'echo', 'cat', 'ls', 'mkdir', 'rm', 'rmdir', 'rename', 'stat', 'truncate', 'pwd')" in read_source(
        'tools/boot_debug.py'
    )
    assert 'tnode.link_count = 0;' in bigfs
    assert 'bigos::vfs::DIRENT_TYPE_DIRECTORY' in bigfs
    assert 'Status readdir(File *__file' in vfs
    assert 'readdir_fd_current' in proc
    assert 'stat_fd_current' in proc
    assert 'bigos::vfs::stat(entry->file, __out)' in proc
    assert 'bigos_readdir(dirfd, entries, BIGOS_DIRENT_MAX_BATCH)' in smoke
    assert 'fstat(fd, &st)' in smoke
    assert 'stat("/rw/runtime_unlink.txt", &st) != -1 || errno != ENOENT' in smoke
    assert 'open("/rw/runtime_unlink.txt", O_RDONLY, 0) != -1 || errno != ENOENT' in smoke
    assert 'test_current_directory(envp)' in smoke
    assert 'getcwd(small, sizeof(small)) != NULL || errno != ERANGE' in smoke
    assert 'open("/boot/user/init.elf", O_WRONLY, 0)' in smoke
    assert 'write(fd, "x", 1) != -1 || errno != ENOSPC' in smoke
    assert 'ftruncate(fd, 10)' in smoke
    assert 'truncate("/rw/runtime_growth.txt", 1)' in smoke
    assert 'runtime-full-offset' in smoke
    assert 'test_fd_inheritance_and_offsets();' in smoke
    assert 'read(fd, child_buf, 3)' in smoke
    assert 'strcmp(parent_buf, "def") != 0' in smoke
    assert 'read(fd2, &ch2, 1) != 1 || ch2 !=' in smoke


def test_pipe_eof_epipe_and_dup_share_offset() -> None:
    pipe = read_source('kernel/core/ipc/pipe.cc')
    proc = read_source('kernel/core/proc/proc.cc')

    # Writers gone + empty buffer -> EOF (read returns 0 success).
    assert 'pipe->count == 0' in pipe
    # Readers gone -> broken pipe on write.
    assert 'Status::BrokenPipe' in pipe
    # lseek on a pipe end is not seekable.
    assert 'Status::NotSeekable' in pipe
    # Blocking only in blockable context.
    assert 'bigos::sched::can_block()' in pipe
    # dup/dup2 share the same vfs::File and retain it once per new fd.
    assert 'int64_t dup_fd_current(' in proc
    assert 'int64_t dup2_fd_current(' in proc
    assert 'bigos::vfs::retain(file);' in proc
    assert 'clone_fd_table(parent, child)' in proc
    assert 'close_all_fds(process);' in proc


def test_smoke_switches_default_off_and_emit_markers() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')

    assert 'option("writable_fs_smoke")' in xmake
    assert 'option("pipe_smoke")' in xmake
    assert 'BIGOS_WRITABLE_FS_SMOKE' in xmake
    assert 'BIGOS_PIPE_SMOKE' in xmake
    # Both options default off.
    wfs_index = xmake.index('option("writable_fs_smoke")')
    assert 'set_default(false)' in xmake[wfs_index : wfs_index + 200]
    pipe_index = xmake.index('option("pipe_smoke")')
    assert 'set_default(false)' in xmake[pipe_index : pipe_index + 200]
    # Markers emitted by the smokes.
    assert 'BIGOS_WRITABLE_FS_PASSED' in kernel
    assert 'BIGOS_PIPE_PASSED' in kernel
