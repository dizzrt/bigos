## Validation Notes

Change: `unicode-console-text-model`

## Passed Checks

- OpenSpec:
  - `openspec validate unicode-console-text-model`
- Source-level tests:
  - `uv run pytest tests/test_tty_console_input_source.py tests/test_framebuffer_boot_handoff_source.py tests/test_device_driver_framework_source.py tests/test_kernel_glyph_font_source.py tests/test_bilingual_docs_layout.py`
  - Result: 33 passed.
- Default build:
  - `xmake`
  - Result: build ok.
- Legacy/default fallback smoke:
  - `uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --serial-log build/test/unicode-console-legacy.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 30`
  - Result: serial marker `BIGOS_USER_EXEC` observed.
- Clang auxiliary syntax checks:
  - `clang++ -fsyntax-only --target=x86_64-elf -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden -O2 -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_AP_STARTUP_PERCPU_TIMERS -DBIGOS_USER_PROCESS -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -DNDEBUG kernel/core/terminal/console.cc`
  - `clang++ -fsyntax-only --target=x86_64-elf -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden -O2 -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_AP_STARTUP_PERCPU_TIMERS -DBIGOS_USER_PROCESS -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -DNDEBUG kernel/core/terminal/console_render.cc`
  - Result: both passed with no diagnostics.
- Clangd auxiliary checks:
  - `clangd --check=kernel/core/terminal/console_render.cc --compile-commands-dir=.`
  - Result: completed with 0 errors.

## Blocked Or Residual Checks

- Full pytest suite:
  - Command: `uv run pytest`
  - Result: failed with 20 existing source-level assertion/file-layout failures outside this change's touched console/framebuffer/doc scope.
  - Examples: missing archived `openspec/changes/add-metadata-consistency/proposal.md`, old active-path scan failure in archived validation text, and several stale source-string expectations in process, VM, syscall, filesystem, scheduler, and user ELF tests.
  - Assessment: targeted tests for this change passed, and these full-suite failures were not introduced by the Unicode console text model files changed in this run.
- Clangd on `kernel/core/terminal/console.cc`:
  - Command: `clangd --check=kernel/core/terminal/console.cc --compile-commands-dir=.`
  - Result: clangd built the preamble and AST, then exited with Apple clangd check-mode `ExtractFunction` tweak failures: `Cannot extract break/continue without corresponding loop/switch statement`.
  - Assessment: no clang compile diagnostic was reported for this file, and the matching `clang++ -fsyntax-only` check plus `xmake` cross-toolchain build both passed. Treat this as an Apple clangd check-mode tweak limitation for this file, not a current-change C++ diagnostic.
- QEMU + OVMF framebuffer graphical validation:
  - QEMU is available: `qemu-system-x86_64` version 11.0.1.
  - No usable OVMF_CODE firmware was found under the checked local paths (`/opt/homebrew`, `/usr/local`, `/usr/share`, or repository build artifacts).
  - Result: blocked for this run.
  - Residual risk: this run does not visually prove UTF-8/CJK glyph display, double-width framebuffer placement, software cursor rendering, or scrollback viewport behavior under the UEFI framebuffer backend. Covered alternatives are source-level checks, default build, and Legacy QEMU headless fallback smoke.

## Historical Diagnostics

- Legacy boot artifact assembly still emits existing `movsd`/`movsl` warnings in `kernel/arch/x86/boot/mbr.s` and `kernel/arch/x86/boot/dbr_exfat.s` during the smoke helper build. These warnings are not introduced by this Unicode console text model change.
