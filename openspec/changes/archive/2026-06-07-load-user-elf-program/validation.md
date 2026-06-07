## Validation Notes

## Decisions Confirmed

- User ELF path: `/boot/user/init.elf`.
- User ELF file bound: `64 KiB` via `USER_ELF_MAX_FILE_BYTES`.
- User virtual window: low canonical user half below `0x0000800000000000`.
- User stack: existing one-page stack ending at `USER_STACK_TOP` (`0x800000`).
- Success markers: `BIGOS_USER_ELF_LOAD_PASSED`, user payload `BIGOS_USER_ELF_WRITE\n`, and existing `BIGOS_USER_EXIT`.
- Failure marker prefix: `BIGOS_USER_ELF_LOAD_FAILED <reason>`.
- ABI/layout review: this change does not modify boot fixed addresses, higher-half base, direct map, `KVMEM_BASE`, recursive self-mapping, syscall vector `0x80`, exception/IRQ DPL policy, or syscall EOI behavior.

## Passed Checks

- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py tests/test_user_elf_program_loader_source.py`
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py tests/test_user_elf_program_loader_source.py`
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py tests/test_user_elf_program_loader_source.py`
- `uv run pytest`
- `xmake f -c && xmake`
- `xmake f --user_elf_smoke=y && xmake`
- `xmake build boot-artifacts`
- `xmake build user-init-elf`
- `uv run python tools/boot_debug.py run --skip-build --no-launch --image build/test/os.raw`
- `uv run python tools/boot_debug.py validate-image --image build/test/os.raw`
- `openspec validate load-user-elf-program --strict`
- VS Code diagnostics for `src/kernel/proc/proc.cc`, `src/kernel/kernel.cc`, and `tools/boot_debug.py`: no diagnostics.

## Runtime Smoke

- Attempted `uv run python tools/boot_debug.py run --skip-build --serial-log build/test/user-elf.serial.log --expect-serial-marker BIGOS_USER_ELF_LOAD_PASSED --smoke-timeout 10`.
- Result: timed out waiting for `BIGOS_USER_ELF_LOAD_PASSED`.
- No `build/test/user-elf.serial.log` was produced, so the failure is recorded as a local Bochs/serial observability blocker after successful source, build, and offline image checks.

## Not Run

- Separate clang/clangd command-line checks were not run with an equivalent freestanding x86_64 configuration; `xmake` with `x86_64-elf-*` and VS Code diagnostics were used instead.

## Remaining Risk

- Runtime bootability of the ELF path still needs confirmation in an environment that produces COM1 output for the generated Bochs config.
