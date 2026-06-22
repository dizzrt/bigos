## 1. 现状复查与范围锁定

- [x] 1.1 复查现有 UEFI loader、ESP/FAT 镜像生成、QEMU/OVMF 启动入口和 `BootInfo v2` handoff，记录哪些能力已经满足默认 backend 晋升前提。
- [x] 1.2 复查默认 normal boot 的 bounded userland payload 集合，确认 UEFI 镜像会打包 kernel、resident PID-1 init、`/bin/sh` 和默认 `/bin/*` 程序。
- [x] 1.3 复查现有 Legacy BIOS/MBR/exFAT QEMU/Bochs 入口和产物路径，确认本 change 的默认 backend 调整不会删除、覆盖或重写它们。
- [x] 1.4 明确当前 change 的非目标记录：Secure Boot、GOP framebuffer、ACPI table handoff、UEFI Runtime Services、第二 ISA、完整 POSIX、动态链接、完整 libc、广泛新存储驱动和完整 smoke matrix 产品化均不纳入实现。

## 2. 默认 backend 选择与启动入口

- [x] 2.1 调整默认 boot/run backend 选择，使默认可运行路径使用 x86_64 UEFI backend、ESP/FAT 镜像和 QEMU/OVMF 启动配置。
- [x] 2.2 保留显式 Legacy BIOS backend 选择入口，确保 MBR/DBR/extended-DBR/`boot.bin` raw image 路径仍可被开发者明确调用。
- [x] 2.3 在默认 UEFI backend 不可用时输出明确失败或 blocked 诊断，避免静默回退到 Legacy BIOS 并误报默认 UEFI 验证通过。
- [x] 2.4 更新 backend 选择相关帮助文本或文档化入口，明确默认 UEFI 与显式 Legacy BIOS 的适用范围。

## 3. UEFI runtime parity 与 handoff 保护

- [x] 3.1 确认 UEFI 默认启动路径使用现有 kernel ELF，不改变 kernel link address、kernel entry、higher-half 地址或 kernel binary format。
- [x] 3.2 确认 UEFI loader 继续通过 x86_64 第一个参数寄存器传递 `BootInfoHeader*`，并保留 `BootInfo` magic/version/size/alignment 检查。
- [x] 3.3 确认 UEFI memory map 到 `BootMemoryRegion` 的转换保持保守：unknown、runtime、MMIO、ACPI、bad、firmware-reserved 等区域不得加入 early free page pool。
- [x] 3.4 确认 UEFI core section 不复用 Legacy `exfat_data_area_lba` 表达 ESP 语义，storage metadata 与 loader metadata 保持 optional。
- [x] 3.5 复查页表、CR3 切换、IDT vector、syscall vector `0x80`、IRQ EOI 规则和用户态 ABI，记录本 change 是否引入变化；若无变化，明确写入 validation notes。

## 4. 默认 UEFI 用户态基线

- [x] 4.1 确认默认 UEFI ESP/FAT 镜像打包 resident PID-1 init、`/bin/sh` 和默认 `/bin/*` 用户态程序，而不是 smoke-only 替代程序。
- [x] 4.2 确认默认 UEFI boot 能进入现有 bounded userland baseline，包括 init 启动、shell 路径和默认用户态执行行为。
- [x] 4.3 确认默认-off smoke 开关仍保持默认关闭，UEFI 成为默认 backend 不会把 `user_program_smoke`、`user_elf_smoke` 或 `userland_smoke` 纳入 normal boot。
- [x] 4.4 在 runtime smoke matrix 或等价验证说明中增加默认 UEFI boot 用例，记录依赖、预期串口证据、timeout、日志路径和 blocked/skipped 语义。

## 5. 文档与规划状态

- [x] 5.1 更新 `docs/en` 相关架构文档，说明 UEFI backend 已晋升为默认 x86_64 runnable boot backend，runtime parity 边界限定为当前 bounded userland baseline。
- [x] 5.2 更新 `docs/zh` 对应相对路径文档，保持与 `docs/en` 语义同步。
- [x] 5.3 在双语文档中保留 Legacy BIOS 显式 backend、UEFI 工具链假设、OVMF/QEMU、mtools、BootInfo metadata 和非目标说明。
- [x] 5.4 如需更新 `roadmap.md`，仅更新项目规划级状态，不加入源码入口、命令、marker、文件路径、实现细节、归档索引或路线图任务编号。
- [x] 5.5 检查本 change 的 proposal、design、specs、tasks 和后续 validation notes，确保不引用路线图任务编号，不把后续能力误写为本 change 已完成。

## 6. 构建与静态检查

- [x] 6.1 运行 `xmake`，确认默认构建及 UEFI 默认 backend 相关产物可构建；若 `x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld` 或 xmake 不可用，记录 blocker、替代检查和剩余风险。
- [x] 6.2 运行 UEFI loader/PE-COFF 产物相关构建或检查，确认 `BOOTX64.EFI` 仍可生成且形态有效；缺少 LLVM/LLD 时记录具体缺失工具。
- [x] 6.3 对本 change 修改的 C/C++ 源码、头文件、UEFI loader 或构建配置执行 clang 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include paths。
- [x] 6.4 对本 change 修改的 C/C++ 源码、头文件、UEFI loader 或构建配置执行 clangd 辅助检查；若 clangd 配置不能等价表达 cross toolchain 环境，记录 gap 和剩余风险。
- [x] 6.5 若本 change 修改 Python helper 或测试，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录 blocker，不静默改用系统 Python。

## 7. Emulator 验证与回归保护

- [x] 7.1 运行默认 UEFI QEMU/OVMF headless smoke，确认串口日志观察到现有 bounded userland baseline 的确定性证据；记录日志路径、timeout、观察结果和失败阶段。
- [x] 7.2 在 QEMU、OVMF、mtools、LLVM/LLD、cross toolchain 或串口日志能力不可用时，将默认 UEFI smoke 标记为 blocked/skipped，并记录替代检查和 default-backend bootability 风险。
- [x] 7.3 运行显式 Legacy BIOS QEMU headless boot 或最窄可用替代检查，确认 Legacy BIOS backend 仍可被明确选择且不会被 UEFI 产物覆盖。
- [x] 7.4 如本地 Bochs、ROM/display 和 disk image 配置可用，运行或记录 Legacy BIOS Bochs 交叉验证；不可用时记录跳过原因和剩余低层 boot/port-IO 风险。
- [x] 7.5 汇总 validation notes，区分已通过检查、未运行检查及原因、历史诊断、当前 change 新增诊断、工具链/仿真器限制和剩余风险。
