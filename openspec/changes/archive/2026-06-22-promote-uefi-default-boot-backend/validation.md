# Validation Notes

## 静态复查

- UEFI loader、ESP/FAT 镜像生成、QEMU/OVMF 启动入口和 `BootInfo v2` handoff 均已存在，并在本 change 中成为默认启动路径。
- 默认 payload 打包包含 kernel、resident PID-1 init、`/bin/sh` 和默认 `/bin/*` 程序；smoke-only init/user 程序仍由显式默认关闭开关选择。
- Legacy BIOS/MBR/exFAT 路径保留为显式 backend：`qemu-legacy`、`qemu-gdb` 和 `bochs` 继续使用现有 MBR/DBR/extended-DBR/`boot.bin` raw image 路径。
- 非目标保持不变：Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、第二 ISA、完整 POSIX、动态链接、完整 libc、广泛新存储驱动和完整 smoke matrix 产品化均不属于本 change。

## ABI 与内存边界

- Kernel ELF、link address、entry address、higher-half base 和 kernel binary format 未改变；`x86_64-elf-objdump -f build/kernel` 确认 entry point 仍为 `0xffffffff80000000`。
- UEFI loader 继续通过 x86_64 第一个参数寄存器传递 `BootInfoHeader*`，并保留 `BootInfo` magic/version/size/alignment 校验。
- UEFI memory map 继续只将 `EfiConventionalMemory` 归一为 usable；runtime、MMIO、ACPI、bad、unknown、firmware-reserved 和 loader/boot-services owned 区域不进入 early free page pool。
- UEFI `core` section 中 `exfat_data_area_lba` 保持为 `0`，ESP/root provenance 仍通过 optional `storage_metadata` 与 `loader_metadata` 表达。
- 本 change 不改变 page-table layout、CR3 切换规则、IDT vector、syscall vector `0x80`、IRQ EOI 规则或用户态 ABI。新增的 UEFI handoff GDT 只在进入 kernel 前建立与 BigOS IDT gate selector 匹配的最小 selector 环境，避免沿用 OVMF GDT 触发 IRQ #GP。

## 已通过检查

- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py`：通过。
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py`：通过。
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py`：通过。
- `uv run pytest tests/test_boot_debug.py`：36 passed。
- `xmake`：通过。
- `xmake build uefi-artifacts`：通过，`BOOTX64.EFI` 为 PE32+ EFI application。
- `xmake run qemu -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`：通过，串口日志 `build/test/qemu-uefi.serial.log` 观察到 `BIGOS_USER_EXEC`。
- `xmake run qemu-legacy -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`：通过，串口日志 `build/test/qemu.serial.log` 观察到 `BIGOS_USER_EXEC`。
- `uv run python tools/boot_debug.py runtime-smoke-matrix --case default-init --output build/test/runtime-smoke-validation.md --serial-log-dir build/test/runtime-smoke --image-dir build/test/runtime-smoke`：通过，artifact 记录 `default-init` 为 `passed`。

## 工具链与仿真器

- 已找到 `qemu-system-x86_64`、`mformat`/`mmd`/`mcopy`/`mdir`、`clang`、`lld-link`、`llvm-objcopy`、`llvm-objdump`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld`、`x86_64-elf-as`。
- 已找到 OVMF code firmware 和 vars template：`/opt/homebrew/share/qemu/edk2-x86_64-code.fd`、`/opt/homebrew/share/qemu/edk2-i386-vars.fd`。
- `clangd --check=kernel/arch/x86/uefi/loader.cc --compile-commands-dir=build` 可运行但不能等价表达当前 cross/UEFI 构建环境，报告 include cleaner 解析错误；以 `xmake build uefi-artifacts` 的 freestanding clang/LLD 实际构建作为替代检查。

## 未运行或受限检查

- Legacy BIOS Bochs 交叉验证未运行；本 change 已通过显式 Legacy QEMU headless 回归。剩余风险限定为 Bochs/BIOS/port-IO 细节差异，后续涉及 Bochs 或硬件行为时应单独执行。
- 完整 runtime smoke matrix 未运行；本 change 只验证默认 UEFI `default-init` 用例和显式 Legacy QEMU 对比路径。其他默认关闭 smoke 仍保持显式选择。

## 诊断记录

- 初次 UEFI smoke 在 `BIGOS_APIC_DEFAULT_FALLBACK_PIC_PIT` 后停住。QEMU interrupt trace 显示第一次 IRQ0 使用 IDT gate selector `0x08` 时，CPU 仍沿用 OVMF GDT，当前 CS 为 `0x38`，触发 #GP 并 triple fault。
- 修复方式是在 `kernel/arch/x86/uefi/handoff.s` 中加载 BigOS 最小 GDT，并通过 far return 刷新 CS 到 `0x08` 后再进入 kernel。复测默认 UEFI smoke 通过。
