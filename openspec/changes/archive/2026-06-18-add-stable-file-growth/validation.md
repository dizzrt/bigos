## Validation

Date: 2026-06-18

### Source And Build Checks

- `uv run pytest tests/test_writable_fs_page_cache_pipe_source.py tests/test_runtime_filesystem_maturity_source.py tests/test_fd_vfs_shell_source.py tests/test_syscall_entry_source.py tests/test_stable_file_growth_source.py`
  - Result: passed, 55 tests.
- `xmake`
  - Result: passed.
  - Note: boot artifact assembly emitted the existing `movsd`/`movsl` warnings in `mbr.s` and `dbr_exfat.s`; no new build failure.
- `x86_64-elf-g++ -std=c++17 -ffreestanding ... -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc kernel/core/proc/proc.cc kernel/core/kernel.cc kernel/core/ipc/pipe.cc`
  - Result: passed.
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding ... -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc kernel/core/proc/proc.cc kernel/core/kernel.cc kernel/core/ipc/pipe.cc`
  - Result: passed.
- `clangd --check=kernel/core/fs/bigfs.cc --compile-commands-dir=.`
  - Result: blocked by Apple clangd check-mode tweak errors (`ExtractFunction ==> FAIL: Cannot extract break/continue without corresponding loop/switch statement`) after AST construction.
  - Classification: clangd tool/check-mode noise; `xmake`, cross `g++ -fsyntax-only`, and `clang++ --target=x86_64-elf -fsyntax-only` passed.
- `openspec validate add-stable-file-growth --strict`
  - Result: passed.

### Runtime Smokes

- RAM-backed `/rw`:
  - Command: `xmake f --writable_fs_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/stable-file-growth-writable.serial.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED --smoke-timeout 30`
  - Result: passed; observed `BIGOS_WRITABLE_FS_PASSED`.
- Persistent clean-sync `/rw`, write phase:
  - Command: `xmake f --persistent_writable_fs_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --persistent-image build/test/stable-file-growth-persistent.img --serial-log build/test/stable-file-growth-persistent-write.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED --smoke-timeout 40`
  - Result: passed; observed `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`.
- Persistent clean-sync `/rw`, verify phase:
  - Command: `uv run python tools/boot_debug.py run --emulator qemu --display none --persistent-image build/test/stable-file-growth-persistent.img --serial-log build/test/stable-file-growth-persistent-verify.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED --smoke-timeout 40`
  - Result: passed; observed `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`.
- Userland wrapper path:
  - Attempted matrix command: `uv run python tools/boot_debug.py runtime-smoke-matrix --case userland-runtime --output build/test/stable-file-growth-userland-runtime.md`
  - Result: blocked by existing helper issue: internal `Namespace` lacks `persistent_image`.
  - Substitute command: `xmake f --userland_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/stable-file-growth-userland.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 40`
  - Result: passed; observed `BIGOS_USERLAND_PASSED`.

### Reset

- `xmake f --writable_fs_smoke=n --persistent_writable_fs_smoke=n --userland_smoke=n`
  - Result: passed; smoke switches restored to default-off for normal local builds.
