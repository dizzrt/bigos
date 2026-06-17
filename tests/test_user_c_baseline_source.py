from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_smoke_probe_programs_are_packaged_only_for_userland_smoke() -> None:
    xmake = read_source('xmake/user_package.lua')
    boot_debug = read_source('tools/boot_debug.py')

    for name in ('args', 'env', 'out', 'errno', 'exit', 'libc_subset'):
        assert f'"{name}"' in xmake
        assert f"'{name}'" in boot_debug
    for name in ('cat', 'ls', 'mkdir', 'rm', 'stat', 'pwd'):
        assert f'"{name}"' in xmake
        assert f'user", "bin", "{name}.c"' in xmake
        assert f"'{name}'" in boot_debug

    assert 'path.join(projectdir, "user", "smoke", "bin"' in xmake
    assert 'if has_config("userland_smoke") then' in xmake
    assert 'os.rm(user_smoke_bin_subdir)' in xmake
    assert "USER_SMOKE_BIN_DIR = USER_BIN_DIR / 'smoke'" in boot_debug
    assert "f'/bin/smoke/{name}'" in boot_debug
    assert 'x86_64-elf-ld -nostdlib -static' in xmake
    assert 'local size_limit = 64 * 1024' in xmake
    assert 'USER_BIN_MAX_BYTES = USER_INIT_ELF_MAX_BYTES' in boot_debug


def test_user_libc_exposes_bounded_fine_grained_headers() -> None:
    libc = read_source('user/libc/include/libc.h')
    stdio = read_source('user/libc/include/stdio.h')
    stdlib = read_source('user/libc/include/stdlib.h')
    string = read_source('user/libc/include/string.h')
    signal = read_source('user/libc/include/signal.h')
    unistd = read_source('user/libc/include/unistd.h')
    time_h = read_source('user/libc/include/time.h')
    fcntl = read_source('user/libc/include/fcntl.h')
    dirent = read_source('user/libc/include/bigos_dirent.h')
    stat = read_source('user/libc/include/sys/stat.h')
    types = read_source('user/libc/include/sys/types.h')
    wait = read_source('user/libc/include/sys/wait.h')
    mman = read_source('user/libc/include/sys/mman.h')

    for include in (
        '"stdio.h"',
        '"signal.h"',
        '"stdlib.h"',
        '"string.h"',
        '"errno.h"',
        '"unistd.h"',
        '"fcntl.h"',
        '"bigos_dirent.h"',
        '"sys/stat.h"',
        '"sys/types.h"',
        '"sys/wait.h"',
        '"sys/mman.h"',
        '"time.h"',
    ):
        assert include in libc

    assert 'typedef struct __bigos_FILE FILE;' in stdio
    assert 'extern FILE *stderr;' in stdio
    assert 'int fprintf(FILE *stream, const char *fmt, ...);' in stdio
    assert 'void perror(const char *s);' in stdio
    assert 'fopen(' not in stdio and 'fclose(' not in stdio
    assert 'void *malloc(size_t n);' in stdlib
    assert 'char *getenv(const char *name);' in stdlib
    assert 'int strncmp(const char *a, const char *b, size_t n);' in string
    assert 'const char *strerror(int errnum);' in string
    assert '#define SIGUSR1 10' in signal
    assert 'typedef unsigned long sigset_t;' in signal
    assert 'struct sigaction' in signal
    assert 'int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);' in signal
    assert 'int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);' in signal
    assert 'ssize_t write(int fd, const void *buf, size_t len);' in unistd
    assert 'int chdir(const char *path);' in unistd
    assert 'char *getcwd(char *buf, size_t size);' in unistd
    assert '#define O_CREAT' in fcntl
    assert 'struct bigos_dirent' in dirent
    assert 'ssize_t bigos_readdir(int fd, struct bigos_dirent *entries, size_t max_entries);' in dirent
    assert 'typedef unsigned int mode_t;' in types
    assert 'int mkdir(const char *path, mode_t mode);' in stat
    assert 'struct stat' in stat
    assert 'BIGOS_METADATA_TYPE_REGULAR' in stat
    assert 'unsigned long st_object_id;' in stat
    assert 'int stat(const char *path, struct stat *st);' in stat
    assert 'int fstat(int fd, struct stat *st);' in stat
    assert '#define WAIT_ANY' in wait
    assert 'pid_t wait(int *status);' in wait
    assert 'pid_t waitpid(pid_t pid, int *status, int options);' in wait
    assert 'pid_t wait_status(pid_t pid, int *status);' in wait
    assert '#define PROT_READ  1' in mman
    assert '#define MAP_FAILED ((void *)-1)' in mman
    assert 'int bigos_munmap_anon(void *addr, size_t len);' in mman
    assert 'int bigos_mprotect_anon(void *addr, size_t len, long prot);' in mman
    assert 'typedef long time_t;' in time_h
    assert 'time_t time(time_t *out);' in time_h


def test_bounded_path_tool_sources_use_libc_contracts() -> None:
    cat = read_source('user/bin/cat.c')
    ls = read_source('user/bin/ls.c')
    mkdir_tool = read_source('user/bin/mkdir.c')
    rm = read_source('user/bin/rm.c')
    stat_tool = read_source('user/bin/stat.c')

    assert 'Not a complete POSIX cat' in cat
    assert 'open(path, O_RDONLY, 0)' in cat
    assert 'read(fd, buf, sizeof(buf))' in cat
    assert 'return copy_fd(0, NULL);' in cat

    assert 'Not a complete POSIX ls' in ls
    assert 'stat(path, &st)' in ls
    assert 'DIR *dir = opendir(path)' in ls
    assert 'struct dirent *entry = readdir(dir)' in ls
    assert 'entry_type(entry->d_type)' in ls

    assert 'Not a complete POSIX mkdir' in mkdir_tool
    assert 'mkdir(argv[i], 0755)' in mkdir_tool
    assert 'usage: mkdir PATH...' in mkdir_tool

    assert 'Not recursive and not a complete POSIX rm' in rm
    assert 'unlink(argv[i])' in rm
    assert 'usage: rm PATH...' in rm

    assert 'bounded metadata observer' in stat_tool
    assert 'stable inode' not in stat_tool


def test_smoke_probe_sources_cover_runtime_contract_categories() -> None:
    args = read_source('user/smoke/bin/args.c')
    env = read_source('user/smoke/bin/env.c')
    out = read_source('user/smoke/bin/out.c')
    errno_probe = read_source('user/smoke/bin/errno.c')
    exit_probe = read_source('user/smoke/bin/exit.c')
    libc_subset = read_source('user/smoke/bin/libc_subset.c')

    assert 'printf("smoke_args argc=%d\\n", argc);' in args
    assert 'strcmp(argv[1], "alpha")' in args
    assert 'printf("smoke_env boundary=empty\\n");' in env
    assert 'getenv("PATH")' in env
    assert 'write_all(1, "smoke_out stdout\\n")' in out
    assert 'write_all(2, "smoke_out stderr\\n")' in out
    assert 'open("/smoke/missing", O_RDONLY, 0)' in errno_probe
    assert 'errno == ENOENT' in errno_probe
    assert 'record("smoke_errno open=-1 errno=2\\n")' in errno_probe
    assert 'return status;' in exit_probe
    assert '#include "../../libc/include/stdio.h"' in libc_subset
    assert '#include "../../libc/include/signal.h"' in libc_subset
    assert '#include "../../libc/include/sys/wait.h"' in libc_subset
    assert 'WAIT_ANY == 0' in libc_subset
    assert 'SIGUSR1 != 10' in libc_subset
    assert 'fprintf(stderr, "smoke_libc_subset stderr errno=%d env=%s width=%5u long=%ld\\n"' in libc_subset
    assert 'malloc((size_t)-64)' in libc_subset
    assert 'memmove(buf + 2, buf, 4)' in libc_subset
    assert 'errno != EFAULT' in libc_subset


def test_userland_smoke_runs_smoke_probes_directly_and_through_shell() -> None:
    smoke = read_source('user/smoke/userland_smoke.c')
    boot_debug = read_source('tools/boot_debug.py')
    proc = read_source('kernel/core/proc/proc.cc')

    assert 'run_program("/bin/smoke/args"' in smoke
    assert 'run_program("/bin/smoke/env"' in smoke
    assert 'run_program("/bin/smoke/out"' in smoke
    assert 'run_program("/bin/smoke/errno"' in smoke
    assert 'run_program_expect("/bin/smoke/exit"' in smoke
    assert 'run_program("/bin/smoke/libc_subset"' in smoke
    assert 'wait_status(pid, &status)' in smoke
    assert 'require_file_contains("/rw/smoke_args.txt"' in smoke
    assert 'require_file_contains("/rw/smoke_libc_subset.txt"' in smoke
    assert 'test_runtime_filesystem();' in smoke
    assert 'test_current_directory(envp);' in smoke
    assert 'test_wait_wrappers(envp);' in smoke
    assert 'test_error_text();' in smoke
    assert 'test_signal_handler_return();' in smoke
    assert 'fstat(fd, &st)' in smoke
    assert 'write_all_or_exit(input[1], "cd /rw\\n");' in smoke
    assert 'write_all_or_exit(input[1], "/bin/pwd\\n");' in smoke
    assert 'write_all_or_exit(input[1], "mkdir smoke_shell_path_dir\\n");' in smoke
    assert 'write_all_or_exit(input[1], "mkdir smoke_shell_path_dir/nested\\n");' in smoke
    assert 'write_all_or_exit(input[1], "ls smoke_shell_path_dir > smoke_shell_ls_rw.txt\\n");' in smoke
    assert (
        'write_all_or_exit(input[1], "/bin/stat smoke_shell_path_dir/nested/file.txt > smoke_shell_stat.txt\\n");'
        in smoke
    )
    assert 'write_all_or_exit(input[1], "/bin/rmdir smoke_shell_path_dir/nested\\n");' in smoke
    assert 'write_all_or_exit(input[1], "/bin/stat smoke_shell_path_dir/nested\\n");' in smoke
    assert 'write_all_or_exit(input[1], "echo shell-alive\\n");' in smoke
    assert 'require_file_contains("/rw/smoke_shell_ls_rw.txt", "dir nested")' in smoke
    assert 'require_file_contains("/rw/smoke_shell_stat.txt", "path=smoke_shell_path_dir/nested/file.txt type=file")' in smoke
    assert 'require_file_contains("/rw/smoke_shell_io.txt", "rmdir: smoke_shell_path_dir/nested: errno=39")' in smoke
    assert 'require_file_contains("/rw/smoke_shell_io.txt", "stat: smoke_shell_path_dir/nested: errno=2")' in smoke
    assert 'require_file_contains("/rw/smoke_shell_io.txt", "status 1")' in smoke
    wait_index = proc.index('int64_t wait_current')
    mark_index = proc.index('mark_reap_pending(match);', wait_index)
    reap_index = proc.index('reap_pending_processes();', mark_index)
    return_index = proc.index('return (int64_t)waited_pid;', reap_index)
    assert mark_index < reap_index < return_index
    assert 'bounded /bin/smoke C programs' in boot_debug
