from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_metadata_timestamp_abi_is_mirrored_between_kernel_and_user() -> None:
    kernel = read_source('include/bigos/metadata.h')
    user = read_source('user/libc/include/sys/stat.h')

    kernel_fields = [
        'uint32_t type;',
        'uint32_t mode;',
        'uint32_t uid;',
        'uint32_t gid;',
        'uint32_t nlink;',
        'uint32_t reserved0;',
        'uint64_t size;',
        'uint64_t object_id;',
        'uint64_t atime;',
        'uint64_t mtime;',
        'uint64_t ctime;',
        'uint64_t reserved;',
    ]
    user_fields = [
        'unsigned int type;',
        'mode_t st_mode;',
        'unsigned int st_uid;',
        'unsigned int st_gid;',
        'unsigned int st_nlink;',
        'unsigned int reserved0;',
        'unsigned long st_size;',
        'unsigned long st_object_id;',
        'unsigned long st_atime;',
        'unsigned long st_mtime;',
        'unsigned long st_ctime;',
        'unsigned long reserved;',
    ]

    last = -1
    for field in kernel_fields:
        index = kernel.index(field)
        assert index > last
        last = index

    last = -1
    for field in user_fields:
        index = user.index(field)
        assert index > last
        last = index


def test_timestamp_syscall_and_wrappers_are_append_only_and_bounded() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    sys_nr = read_source('user/libc/include/sys_nr.h')
    syscall = read_source('kernel/core/syscall/syscall.cc')
    libc = read_source('user/libc/syscall.c')

    assert 'SYS_SLEEP_MS = 53' in syscall_h
    assert 'SYS_UTIMENS = 54' in syscall_h
    assert '#define SYS_UTIMENS     54' in sys_nr
    assert 'static int64_t sys_utimens(' in syscall
    assert 'copy_user_path(__path, path, sizeof(path))' in syscall
    assert 'bigos::vfs::utimens(path, bigos::proc::current_cwd()' in syscall
    assert 'case SYS_UTIMENS:' in syscall
    assert 'int bigos_utimens(' in libc
    assert 'int utime(const char *path, const struct utimbuf *times)' in libc


def test_bigfs_stores_and_updates_bounded_timestamps() -> None:
    header = read_source('include/bigos/fs/bigfs.h')
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'FORMAT_VERSION = 2' in header
    assert 'INODE_SIZE = 128' in header
    assert 'uint64_t atime;' in bigfs
    assert 'uint64_t mtime;' in bigfs
    assert 'uint64_t ctime;' in bigfs
    assert 'touch_all(&file, now)' in bigfs
    assert 'touch_data_change(&committed, current_timestamp())' in bigfs
    assert 'store_inode_with_time(__inode, &file, now, true, false, false)' in bigfs
    assert 'Status utimens(' in bigfs
    assert 'metadata_commit_inode(target) && metadata_commit_flush()' in bigfs
