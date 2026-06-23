# Validation Notes

## Scope

This validation record tracks glyph lookup asset readiness separately from framebuffer handoff readiness and default boot readiness.

- Glyph lookup asset readiness: build-time conversion, ESP packaging, UEFI loader loading, and kernel-side lookup validation.
- Framebuffer handoff readiness: GOP metadata handoff and protected framebuffer memory treatment.
- Default boot readiness: bounded userland baseline reaches the expected serial marker.

This change does not validate framebuffer glyph rendering, Unicode display, software cursor support, framebuffer scrollback, Secure Boot, ACPI handoff, UEFI Runtime Services, or full device/storage parity.

## Evidence To Record

- Asset generation: `build/assets/fonts/unifont.bin` uses glyph lookup format version 2, not raw HEX payload.
- ESP packaging: generated asset is copied to `/boot/fonts/unifont.bin`.
- UEFI loader: serial log contains `BIGOS_UEFI_FONT` on supported payloads, or an explicit `BIGOS_UEFI_FONT unavailable stage=...` fallback.
- Kernel lookup validation: serial log contains `BIGOS_FONT_LOOKUP ready` on valid payloads, or an explicit `BIGOS_FONT_LOOKUP unavailable stage=...` fallback.
- Default UEFI pass condition remains `BIGOS_USER_EXEC`.
- Explicit Legacy fallback validation should state that Legacy boot does not depend on GOP, framebuffer metadata, or glyph lookup metadata.

## Results

### Passed

- `openspec validate add-kernel-glyph-font-lookup --strict`
  - Result: passed.
- `openspec status --change "add-kernel-glyph-font-lookup" --json`
  - Result: schema is `spec-driven`; proposal, design, specs, and tasks are complete/apply-ready.
- `uv run pytest tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_kernel_glyph_font_source.py -q`
  - Result: `43 passed`.
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_kernel_glyph_font_source.py`
  - Result: passed after formatting `tests/test_boot_debug.py`.
- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_kernel_glyph_font_source.py`
  - Result: passed.
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_kernel_glyph_font_source.py`
  - Result: `0 errors, 0 warnings, 0 informations`; pyright printed only an available-version notice.
- `xmake build kernel`
  - Result: passed.
- `xmake build uefi-artifacts`
  - Result: passed; clang emitted an existing UEFI build warning for unused `-ffreestanding`.
- `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/terminal/glyph_font.cc kernel/core/kernel.cc kernel/mm/vmem.cc`
  - Result: passed.
- `clangd --check=kernel/core/terminal/glyph_font.cc --compile-commands-dir=/Users/bytedance/Desktop/workspace/kernel/bigos`
  - Result: `0 errors`.
- `clangd --check=kernel/core/kernel.cc --compile-commands-dir=/Users/bytedance/Desktop/workspace/kernel/bigos`
  - Result: `0 errors`.
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - Result: passed; serial marker `BIGOS_USER_EXEC` observed.
  - Glyph asset evidence: generated `build/assets/fonts/unifont.bin` is 6,853,008 bytes, magic `0x544e4642`, header size 64, format version 2, 3,714 ranges, 127,011 glyphs, 3,730,400 bitmap bytes, 16x16 max glyph/cell metrics.
  - ESP evidence: `mdir` validated `::/boot/fonts/unifont.bin` in the ESP image.
  - UEFI loader evidence: serial log contains `BIGOS_UEFI_FONT base=0x000000000575d018 size=0x0000000000689190 version=2 cell=16x16`.
  - Kernel lookup evidence: serial log contains `BIGOS_FONT_LOOKUP ready`.
  - Framebuffer handoff evidence: serial log contains `BIGOS_UEFI_FRAMEBUFFER base=0x0000000080000000 size=0x00000000003e8000 width=1280 height=800 stride=1280 format=1`.
- `uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --image build/test/os.raw --serial-log build/test/qemu-legacy.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - Result: passed; serial marker `BIGOS_USER_EXEC` observed.
  - Legacy evidence: `build/test/qemu-legacy.serial.log` contains `BIGOS_USER_EXEC` and no `BIGOS_UEFI_FONT`, `BIGOS_FONT_LOOKUP`, or `BIGOS_UEFI_FRAMEBUFFER` markers.

### Known Historical Or Tooling Diagnostics

- `xmake build boot-artifacts` emits existing GNU assembler warnings in `kernel/arch/x86/boot/mbr.s` and `kernel/arch/x86/boot/dbr_exfat.s`: `found 'movsd'; assuming 'movsl' was meant`.
- `clangd --check=kernel/arch/x86/uefi/loader.cc --compile-commands-dir=/Users/bytedance/Desktop/workspace/kernel/bigos` used an inferred kernel compile command for the UEFI loader and reported clangd tweak-layer `ExtractFunction` failures. The actual UEFI target build passed through `xmake build uefi-artifacts`.
- `clangd --check=kernel/mm/vmem.cc --compile-commands-dir=/Users/bytedance/Desktop/workspace/kernel/bigos` reported historical clangd tweak-layer `ExtractFunction` failures in this large file; the targeted `clang++ -fsyntax-only` command and `xmake build kernel` both passed.

### Not Covered

- Manual graphical framebuffer inspection was not performed.
- This validation does not prove framebuffer glyph rendering, Unicode display, software cursor support, framebuffer scrollback, Secure Boot, ACPI handoff, UEFI Runtime Services, or full device/storage parity.
