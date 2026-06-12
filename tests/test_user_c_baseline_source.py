from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding='utf-8')


def test_smoke_probe_programs_are_packaged_only_for_userland_smoke() -> None:
    xmake = read_source('xmake/user_package.lua')
    boot_debug = read_source('tools/boot_debug.py')

    for name in ('args', 'env', 'out', 'errno', 'exit'):
        assert f'"{name}"' in xmake
        assert f"'{name}'" in boot_debug

    assert 'path.join(projectdir, "user", "smoke", "bin"' in xmake
    assert 'if has_config("userland_smoke") then' in xmake
    assert 'os.rm(user_smoke_bin_subdir)' in xmake
    assert "USER_SMOKE_BIN_DIR = USER_BIN_DIR / 'smoke'" in boot_debug
    assert "f'/bin/smoke/{name}'" in boot_debug
    assert 'x86_64-elf-ld -nostdlib -static' in xmake
    assert 'local size_limit = 64 * 1024' in xmake
    assert 'USER_BIN_MAX_BYTES = USER_INIT_ELF_MAX_BYTES' in boot_debug


def test_smoke_probe_sources_cover_runtime_contract_categories() -> None:
    args = read_source('user/smoke/bin/args.c')
    env = read_source('user/smoke/bin/env.c')
    out = read_source('user/smoke/bin/out.c')
    errno_probe = read_source('user/smoke/bin/errno.c')
    exit_probe = read_source('user/smoke/bin/exit.c')

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


def test_userland_smoke_runs_smoke_probes_directly_and_through_shell() -> None:
    smoke = read_source('user/smoke/userland_smoke.c')
    boot_debug = read_source('tools/boot_debug.py')

    assert 'run_program("/bin/smoke/args"' in smoke
    assert 'run_program("/bin/smoke/env"' in smoke
    assert 'run_program("/bin/smoke/out"' in smoke
    assert 'run_program("/bin/smoke/errno"' in smoke
    assert 'run_program("/bin/smoke/exit"' in smoke
    assert 'require_file_contains("/rw/smoke_args.txt"' in smoke
    assert 'write_all_or_exit(input[1], "/bin/smoke/exit 7\\n");' in smoke
    assert 'write_all_or_exit(input[1], "echo shell-alive\\n");' in smoke
    assert 'unlink("/rw/smoke_args.txt")' in smoke
    assert 'bounded /bin/smoke C programs' in boot_debug
