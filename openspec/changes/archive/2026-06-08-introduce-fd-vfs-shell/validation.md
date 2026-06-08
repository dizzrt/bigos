# Validation Notes

## Passed Checks

- `openspec validate introduce-fd-vfs-shell --strict`
  - Result: passed.
- `uv run pytest tests/test_fd_vfs_shell_source.py tests/test_syscall_entry_source.py tests/test_first_user_program_source.py tests/test_bilingual_docs_layout.py`
  - Result: passed, `32 passed`.
- `uv run pytest`
  - Result: passed, `157 passed`.
- `xmake`
  - Result: passed for the normal kernel build.
  - Diagnostic: linker still reports the pre-existing `build/kernel has a LOAD segment with RWX permissions` warning.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/fd-vfs-fs-smoke.serial.log --expect-serial-marker BIGOS_FS_EXFAT_READ_PASSED --smoke-timeout 20`
  - Result: passed.
  - Observed marker: `BIGOS_FS_EXFAT_READ_PASSED`.
  - Serial log: `build/test/fd-vfs-fs-smoke.serial.log`.
- VS Code/clangd diagnostics via editor diagnostics
  - Result: no current diagnostics reported for edited files.

## Current-Change Diagnostics

- Added `include/bigos/fs/vfs.h` and `src/kernel/fs/vfs.cc` for the minimal read-only VFS shell and exFAT backend adapter.
- Added bounded process fd table ownership, fd install/read/close helpers, `exec` close-on-exec handling, and safe reaper close-all.
- Added `SYS_OPEN`, `SYS_READ`, and `SYS_CLOSE` under the existing `int 0x80` ABI.
- Added writable user-range validation for `SYS_READ` copy-out so VFS file offsets do not advance before an invalid destination is rejected.
- Changed the syscall IDT entry from a DPL=3 interrupt gate to a DPL=3 trap gate so ordinary process syscalls preserve IF and can pass `sched::can_block()` before fd/VFS blocking operations.
- Migrated `fs_smoke` and `user_elf_smoke` consumers to VFS open/read/release while preserving existing markers.
- Added bilingual fd/VFS documentation and source-level tests.

## Skipped Checks And Blockers

- Bochs cross-validation was not run in this session.
  - Reason: QEMU headless serial-marker validation covered the requested fd/VFS smoke marker; Bochs was left as a residual hardware-behavior cross-check for ATA PIO/port-IO timing.
- A standalone manual `clang++` compile was not run.
  - Reason: the repository build uses the xmake cross-toolchain flags and the editor clangd diagnostics were clean; `xmake` cross build is the authoritative compile check for this freestanding target.

## Residual Risk

- The syscall trap-gate change is intentional for fd/VFS blocking semantics but remains a low-level ABI/interrupt-behavior change; QEMU covered the filesystem marker, but broader user syscall runtime coverage should be expanded before relying on file syscalls for general userland.
- The VFS backend is still single-root, read-only, synchronous ATA PIO/exFAT only. It does not provide page cache, writable files, cwd, relative path resolution, fd duplication, or async I/O.
- `SYS_READ` uses a bounded kernel stack buffer capped by `SYS_IO_MAX_LEN = 512`; larger user reads must be split by callers until a future buffered/page-cache design exists.
