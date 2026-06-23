## Validation Notes

### Passed

- `uv run pytest tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py`
  - 结果：39 passed。
  - 覆盖：UEFI ESP 打包 helper、font asset path、BootInfo framebuffer/font section、GOP/font loader evidence、kernel optional view、framebuffer memory exclusion 和 device/MMIO mapping API source checks。
- `uv run pytest tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_memory_correctness_source.py::test_direct_map_initialization_uses_bootinfo_ram_and_panics_on_partial_failure`
  - 结果：40 passed。
  - 覆盖：本 change 修改后的 direct-map 初始化从 BootInfo memory map 消费路径进入 framebuffer-aware exclusion helper。
- `xmake build kernel`
  - 结果：通过。
- `xmake build uefi-artifacts`
  - 结果：通过。
  - 备注：clang 报告既有 `-ffreestanding` 参数未使用 warning；未阻塞 PE/COFF EFI application 输出。
- `uv run python tools/boot_debug.py run --boot-mode uefi --no-launch --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi.serial.log`
  - 结果：通过。
  - 证据：生成 `build/assets/fonts/unifont.bin`，ESP 校验存在 `/boot/fonts/unifont.bin`，大小 `8355876` bytes。
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，观察到 `BIGOS_USER_EXEC`。
  - Framebuffer/font evidence：`BIGOS_UEFI_FRAMEBUFFER base=0x0000000080000000 size=0x00000000003e8000 width=1280 height=800 stride=1280 format=1`；`BIGOS_UEFI_FONT base=0x00000000055f0018 size=0x00000000007f8024 version=1 cell=8x16`。
- `uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --image build/test/legacy-framebuffer-fallback.raw --serial-log build/test/legacy-framebuffer-fallback.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过。
  - 结论：显式 Legacy BIOS fallback 不依赖 GOP、framebuffer metadata 或 font metadata。
- `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -Iinclude -Ikernel/mm -fsyntax-only kernel/mm/boot_handoff.cc`
  - 结果：通过。
- `clang++ -target x86_64-pc-win32 -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fno-exceptions -fno-rtti -std=c++17 -Iinclude -Ikernel/arch/x86/uefi -fsyntax-only kernel/arch/x86/uefi/loader.cc`
  - 结果：通过。
- `clangd --check=kernel/mm/boot_handoff.cc --compile-commands-dir=.`
  - 结果：通过，0 errors。
- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_memory_correctness_source.py`
  - 结果：通过。
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_memory_correctness_source.py`
  - 结果：通过。
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py tests/test_framebuffer_boot_handoff_source.py tests/test_memory_correctness_source.py`
  - 结果：通过，0 errors。
- `openspec validate add-framebuffer-boot-handoff --strict`
  - 结果：通过。

### Limited / Current Change Diagnostics

- `clangd --check=kernel/arch/x86/uefi/loader.cc --compile-commands-dir=.`
  - 结果：未作为失败归因到本 change。
  - 原因：当前 `compile_commands.json` 不包含 UEFI loader 的 `x86_64-pc-win32` freestanding PE/COFF compile command，clangd 从 kernel source 推断出 `x86_64-elf` kernel 配置，并报告 ExtractFunction tweak 级错误。
  - 替代检查：UEFI loader 已通过准确目标的 `clang++ -target x86_64-pc-win32 ... -fsyntax-only` 和 `xmake build uefi-artifacts`。

### Existing / Unrelated Failures Observed

- `uv run pytest`
  - 结果：281 passed, 21 failed。
  - 归因：失败集中在既有 source-level 断言漂移和缺失 active OpenSpec artifact，例如 `test_source_root_layout.py` 扫描 archived validation 中的旧 `src/kernel` 文本、`test_metadata_consistency_source.py` 缺失 `openspec/changes/add-metadata-consistency/proposal.md`、以及多个与进程/VM/syscall 历史源码字符串匹配有关的断言。
  - 本 change 相关处理：唯一受本 change 影响的 direct-map BootInfo memory map 断言已更新为 framebuffer-aware exclusion helper，并通过 targeted pytest。

### Skipped / Residual Risk

- 未执行人工图形化 framebuffer 输出验证。
  - 原因：本 change 只交付 handoff metadata、memory reservation 和 mapping API，不实现 glyph renderer 或 framebuffer console writer。
  - 剩余风险：后续 renderer change 仍需验证实际像素写入、字体 lookup、软件光标、scrollback 与 Unicode/cell 模型。
- 未验证 Secure Boot、ACPI handoff、UEFI Runtime Services、Legacy VBE、virtio/AHCI/SATA/NVMe 或完整 storage/device parity。
  - 原因：均为本 change 明确非目标。
