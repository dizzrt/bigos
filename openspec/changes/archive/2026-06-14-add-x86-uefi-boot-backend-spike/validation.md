# Validation Notes

## Legacy Boundary Review

- Legacy BIOS artifacts remain under `build/bin/x86/boot`: `mbr.bin`, `dbr.bin`, `exdbr.bin`, and `boot.bin`.
- UEFI artifacts are isolated under `build/bin/x86/uefi`, with `BOOTX64.EFI` as the only UEFI loader output.
- Legacy raw image generation remains `build/test/os.raw`; UEFI ESP generation uses `build/test/uefi-esp.img`.
- Existing `qemu`, `qemu-gdb`, and `bochs` run targets still depend on `boot-artifacts`; only `qemu-uefi` depends on `uefi-artifacts`.

## ABI And Memory Review

- Kernel link address and higher-half virtual base remain `0xffffffff80000000`.
- Kernel physical load/reservation assumption remains `0x1000000`; the UEFI loader materializes this fixed window after `ExitBootServices` to avoid OVMF allocation conflicts.
- The `BootInfo` v2 magic, version, maximum size, alignment, and required `core`/`memory_map` validation rules remain unchanged.
- UEFI `core.exfat_data_area_lba` is zero; ESP/root provenance is represented by optional `storage_metadata`.
- Optional `storage_metadata` and `loader_metadata` sections are non-required and remain skippable by existing tagged-section validation.
- UEFI descriptors are conservatively normalized: only `EfiConventionalMemory` can become `usable`; runtime, MMIO, ACPI, loader-owned, boot-services-owned, bad, unknown, and reserved descriptors are not admitted to the initial free page pool.
- The UEFI producer merges adjacent descriptors and falls back to a usable-only memory map when OVMF descriptor count exceeds the existing 4 KiB `BootInfo` v2 cap. This preserves bootability without changing the ABI cap, but leaves reduced audit detail as residual UEFI metadata risk.

## Validation Runs

- `xmake`: passed; existing kernel target builds.
- `xmake build boot-artifacts`: passed; Legacy BIOS artifacts build with existing assembler `movsd` warnings.
- `xmake build user-init-elf`: passed; default init and `/bin/*` payloads build.
- `xmake build uefi-artifacts`: passed; `llvm-objdump` reports `BOOTX64.EFI` as `coff-x86-64`, `PE32+`, subsystem `EFI application`.
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --serial-log build/test/qemu-uefi.serial.log --skip-build --no-launch`: passed; ESP image contains `EFI/BOOT/BOOTX64.EFI`, `/boot/kernel`, `/boot/user/init.elf`, and `/bin/sh`.
- `uv run pytest tests/test_boot_debug.py`: passed, 35 tests.
- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py`: passed.
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py`: passed.
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py`: passed.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/qemu-legacy.serial.log --skip-build --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`: passed; Legacy BIOS default marker observed.
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --serial-log build/test/qemu-uefi.serial.log --skip-build --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`: failed; QEMU/OVMF timed out waiting for `BIGOS_USER_EXEC`.

## UEFI Smoke Result

- Observed UEFI serial log: OVMF loads `BOOTX64.EFI`, then the loader emits `BIGOS_UEFI_LOADER_START`.
- Not observed: `BIGOS_USER_EXEC`.
- Current status: UEFI loader starts and no longer reports a loader-local error, but runtime parity is blocked because the kernel/default init marker is not reached before timeout.
- Residual risk: handoff state after `ExitBootServices` still needs low-level debugging, likely around fixed page tables, CPU descriptor state, or early kernel assumptions that are implicit in the Legacy BIOS `boot.s` path.

## Tool Availability

- Available in this environment: `uv`, `xmake`, x86_64 cross toolchain, QEMU, Homebrew OVMF code/vars files, Homebrew LLVM/LLD, and `mtools`.
- No validation was skipped due to missing local tools.
