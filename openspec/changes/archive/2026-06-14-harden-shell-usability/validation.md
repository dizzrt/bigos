## Validation Notes

### Current-Change Diagnostics

- Passed: `xmake f --userland_smoke=y`
- Passed: `xmake`
- Passed: `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland_smoke_serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`
- Passed: editor/clangd diagnostics for `user/sh/sh.c`
- Passed: editor/clangd diagnostics for `user/smoke/userland_smoke.c`

### Runtime Coverage

- `userland_smoke` now covers `/bin/sh` success, deterministic command-not-found failure, unsupported syntax, cwd-relative path use, output redirection, single-pipe composition, bounded `status` observation, nonzero terminal pipeline status, and recovery with a later `echo shell-alive`.
- QEMU headless serial validation observed `BIGOS_USERLAND_PASSED` in `build/test/userland_smoke_serial.log`.

### Skipped Or Blocked

- Skipped: Bochs cross-validation. This change only touches bounded userland shell and smoke behavior; it does not modify early boot, port I/O, interrupt, storage-driver, or other low-level hardware behavior.
- Skipped: manual interactive graphical shell validation. The scripted `userland_smoke` path exercises the same `/bin/sh` binary through bounded stdin/stdout/stderr and fd/VFS contracts.

### Historical Diagnostics

- None used for pass/fail claims in this change.

### Substitute Checks

- No substitute checks were needed for QEMU runtime validation because the cross-toolchain, xmake, `uv`, image packaging, QEMU headless backend, serial oracle, and timeout path were available.

### Remaining Risk

- Residual risk is limited to interactive-only ergonomics not represented by scripted stdin; the parent fd recovery, status propagation, pipe, redirection, and path-tool composition paths are covered by the QEMU `userland_smoke` run.
