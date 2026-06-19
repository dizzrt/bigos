## Validation Notes

### Passed

- `uv run pytest tests/test_syscall_entry_source.py tests/test_fd_vfs_shell_source.py tests/test_user_c_baseline_source.py tests/test_process_lifecycle_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_bilingual_docs_layout.py`
  - Result: 57 passed.
- `xmake`
  - Result: default kernel/user baseline build completed successfully.
- `xmake f --userland_smoke=y`
  - Result: configuration accepted.
- `xmake`
  - Result: userland smoke configuration build completed successfully.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-expanded-syscall.log --expect-serial-marker BIGOS_USERLAND_PASSED`
  - Result: QEMU headless observed `BIGOS_USERLAND_PASSED`.
- `xmake f --userland_smoke=n`
  - Result: local build configuration restored to the default-off userland smoke setting.
- `xmake`
  - Result: default configuration rebuilt successfully after restoring the smoke switch.

### Tooling Notes

- `clang++ --version` and `clangd --version` are available as Apple clang/clangd 21.0.0.
- No repository `compile_commands.json` or `.clangd` file was present, so no targeted clangd `--check` run was claimed; the cross-toolchain `xmake` builds above are the authoritative compile validation for this freestanding target.

### Observed Existing Diagnostics

- The QEMU helper build emitted existing assembler warnings in `kernel/arch/x86/boot/mbr.s` and `kernel/arch/x86/boot/dbr_exfat.s`: `found 'movsd'; assuming 'movsl' was meant`. They are pre-existing boot assembly warnings, not introduced by this change.

### Residual Risk

- The new `waitpid` surface is a bounded subset: only `WAIT_ANY`, positive child pid selectors, and `WNOHANG` are supported. It does not implement process-group waits, stopped/continued states, `waitid`, resource usage, interruptible waits, or full job control.
- The new `fcntl` surface is limited to `F_GETFD`, `F_SETFD` with `FD_CLOEXEC`, and `F_DUPFD`. It does not implement record locks, nonblocking mode, async I/O, descriptor passing, or `F_DUPFD_CLOEXEC`.
- `access`, `stat`, `fstat`, `truncate`, and `ftruncate` remain BigOS bounded VFS primitives over the current exFAT and `/rw` backends, not a complete POSIX filesystem contract.
