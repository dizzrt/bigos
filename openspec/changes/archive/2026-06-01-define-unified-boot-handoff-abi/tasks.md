## 1. 公共 handoff ABI

- [x] 1.1 在 `include/arch/x86/boot/boot_info.h` 中保留现有 v1 `BootInfo` 常量、字段和 static_assert，不改变 `BIGOS_BOOT_INFO_ADDRESS` 或 v1 layout。
- [x] 1.2 定义 v2 `BootInfoHeader`、`BootInfoSection`、boot protocol、section type、section flags、core flags、独立 v2 magic 和 ABI 常量。
- [x] 1.3 为 v2 header 和 section table 增加 size、alignment、field offset 的 C++ static_assert，并保持 C-compatible layout。
- [x] 1.4 定义 handoff parser/validator 接口，支持优先校验 register-passed pointer，并在失败时显式 fallback 到 v1 fixed low-address `BootInfo`。
- [x] 1.5 记录 v2 magic/version 选择、relative section offset/size、required/optional section 策略和 legacy fallback 规则。

## 2. Legacy BIOS v2 producer

- [x] 2.1 在 Legacy BIOS boot C++ 中继续写入现有 v1 `BootInfo` fallback，保持 E820 metadata、boot drive、kernel entry/load 和 kernel size 字段兼容。
- [x] 2.2 选择并文档化 Legacy BIOS v2 handoff blob 的 producer-side 存放区域，确认不覆盖 E820 buffer、legacy aliases、v1 `BootInfo`、boot-stage page table 区域或 kernel load base。
- [x] 2.3 在 Legacy BIOS backend 中生成完整 v2 handoff blob，至少包含 `BootInfoHeader`、section table、boot protocol/core section 和 memory map section。
- [x] 2.4 将 BIOS E820 ARDS 规范化为 `BootMemoryRegion[]` 并写入 v2 memory map section，未知 E820 type 保守写为 reserved。
- [x] 2.5 为 v2 blob 生成逻辑增加边界、alignment、total size 和 section payload 范围校验。

## 3. 寄存器传递 BootInfo 指针

- [x] 3.1 修改 Legacy BIOS long-mode jump 路径，在跳转 kernel ELF entry 前设置 x86_64 第一个参数寄存器为 v2 `BootInfoHeader*`。
- [x] 3.2 修改 runtime `_start`，保存入口 `BootInfo*`，调用 `_init` 后恢复并作为第一个参数传递给 `kernel()`。
- [x] 3.3 修改 kernel entry 签名，使 `kernel()` 接收 `const BootInfo*` 或等价 handoff 指针。
- [x] 3.4 确认 `_fini`、全局构造、栈对齐和调用约定没有因为入口参数转发而破坏。
- [x] 3.5 保留 fixed low-address fallback，确保寄存器参数为空或 v2 校验失败时有明确兼容路径。

## 4. 统一 BootMemoryRegion view

- [x] 4.1 定义 `BootMemoryRegion`、normalized memory type、memory attributes 和 source type，覆盖 usable、reserved、acpi_reclaim、acpi_nvs、mmio、loader、kernel、bad_memory、runtime。
- [x] 4.2 实现从 v2 memory map section 到 `BootMemoryRegion` view 的无动态分配遍历路径。
- [x] 4.3 保留从 v1 BIOS E820 ARDS 到 `BootMemoryRegion` 的 fallback 转换路径，未知 E820 type 保守映射为 reserved。
- [x] 4.4 将 buddy 初始化改为消费 `BootMemoryRegion` view 或 callback，只把 usable 区域加入 free list，并保守排除 acpi_reclaim、acpi_nvs、runtime、mmio、bad_memory、reserved 和 unknown。
- [x] 4.5 审查早期内存初始化顺序、对象生命周期、对齐、zone 分配边界和无可用 memory map 时的失败行为。

## 5. 文档同步

- [x] 5.1 更新 `docs/arch/x86-boot-layout.md`，记录 register-passed v2 `BootInfoHeader*`、v2 blob producer-side 存放区域、v1 fixed-address fallback 和未移动的低地址布局。
- [x] 5.2 更新 `docs/arch/uefi-boot-blueprint.md`，标注本 change 已落地的 ABI 基础和仍未实现的 UEFI loader/ESP/OVMF 范围。
- [x] 5.3 记录 `BootMemoryRegion` E820 映射表、reserved/runtime/mmio/acpi_reclaim/acpi_nvs/bad_memory 的保守处理策略。
- [x] 5.4 记录 `make boot-debug` 仍保持 Legacy BIOS/MBR/exFAT/Bochs 语义，不切换到 UEFI。

## 6. 构建与静态检查

- [x] 6.1 运行 `xmake` 或等价 x86_64 cross-toolchain build，验证 boot、runtime、kernel 和 mm 改动可构建。
- [x] 6.2 针对修改过的 C++ headers/source 运行尽可能接近 freestanding C++17/x86_64/no-exceptions/no-RTTI 的 clang 辅助静态检查；若不可用，记录原因和剩余风险。
- [x] 6.3 通过 clangd 或 IDE diagnostics 检查修改过的 C++/assembly 相关文件，区分历史诊断、当前 change 引入的问题和 freestanding 配置误报。
- [x] 6.4 修复当前 change 引入的有效编译、clang、clangd 或 layout static_assert 问题。

## 7. 启动与兼容验证

- [x] 7.1 运行 Legacy BIOS `make boot-debug` 或等价 Bochs smoke test，确认 v2 blob producer、register handoff、runtime `_start` 转发和 memory init 仍能到达 kernel。
- [x] 7.2 如果 Bochs、disk image 路径或 cross toolchain 不可用，在验证记录中明确无法运行的原因、已完成的替代检查和剩余 bootability 风险。
- [x] 7.3 进行 ABI/layout compatibility review，确认未移动 E820 buffer、legacy aliases、v1 `BootInfo` address、boot-stage page table 区域、kernel load base 和 higher-half base，并确认 v2 blob 区域不会与这些区域冲突。
- [x] 7.4 进行 early memory initialization review，确认只释放 usable region，reserved/runtime/mmio/acpi_reclaim/acpi_nvs/bad_memory/unknown region 不进入 buddy free list。
- [x] 7.5 运行 `openspec validate define-unified-boot-handoff-abi --strict`，确认 proposal、design、tasks 和 spec delta 可解析。

## 8. 验证记录

- [x] 8.1 在本文件追加验证记录，分开列出已通过检查、无法运行检查及原因、历史诊断、当前 change 引入并已修复的问题。
- [x] 8.2 记录本 change 未实现 `BOOTX64.EFI`、ESP/FAT image、QEMU/OVMF UEFI 入口或 UEFI Runtime Services。

## 验证记录

### 已通过检查

- `xmake`：kernel、runtime、mm 相关改动构建通过；输出仍包含历史 `command-line` 宏空白 warning、`build/kernel` RWX LOAD segment warning 和 `$(buildir)` deprecation warning。
- `make -C src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot`：Legacy BIOS boot artifacts 构建通过；`mbr.s`/`dbr_exfat.s` 仍有历史 `movsd`/`movsl` assembler warning。
- `python3 tools/boot_debug.py run --no-launch`：kernel build、boot build、raw image build 和 image validation 通过，生成 `build/test/os.raw` 与 `build/test/bochsrc.bxrc`。
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -nostdinc++ -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only ...`：修改过的 C++ source/header 语法检查通过。
- IDE diagnostics / clangd：修改过的 C++ 文件当前无诊断。
- `openspec validate define-unified-boot-handoff-abi --strict`：通过。

### 未完成检查

- Legacy BIOS Bochs smoke test：已尝试运行 bounded `bochs -f build/test/bochsrc.bxrc -q -benchmark 5`，但命令被用户/环境中断并标记为跳过，未取得 “到达 kernel” 的运行时证据。剩余风险是 v2 blob producer、`rdi` handoff、runtime `_start` 转发和 early memory init 的完整启动链路尚未通过 emulator 观察确认。

### 兼容性审查

- v1 `BootInfo` magic/version/size/field offsets/alignment 和 `BIGOS_BOOT_INFO_ADDRESS` 保持不变。
- 未移动 `0x0500` E820 buffer、`0x0800..0x083f` legacy aliases、`0x0840` v1 `BootInfo`、`0x2000..0x6fff` boot-stage page tables、`0x100000` higher-half page-table backing area、`0x1000000` kernel load base 或 `0xffffffff80000000` higher-half base。
- v2 blob 使用 Legacy BIOS producer-side `0x9000..0x9fff`，避开上述区域和 `0x0f000` exFAT directory buffer。
- early memory initialization 优先消费 v2 `BootMemoryRegion[]`，v2 无效时 fallback 到 v1 fixed-address `BootInfo` 指向的 E820 ARDS；无有效 memory map 时显式 halt。
- buddy 只释放 normalized `usable` region；`reserved`、`runtime`、`mmio`、`acpi_reclaim`、`acpi_nvs`、`bad_memory` 和 unknown/保守 reserved region 不进入 free list。

### 已修复问题

- 初始 `memory.cc` / `kmem.cc` diagnostics 因 `init_mem()` / `init_buddy()` 签名更新未同步产生，已同步 public/internal declarations。
- 初始 boot build 对 `extern "C" uint64_t g_boot_handoff_address = 0` 产生 warning，已改为 C linkage block 定义。

### 非目标记录

- 本 change 未实现 `BOOTX64.EFI`。
- 本 change 未生成 ESP/FAT UEFI image。
- 本 change 未新增 QEMU/OVMF UEFI 入口。
- 本 change 未实现或调用 UEFI Runtime Services。
