## 1. 蓝图文档落地

- [x] 1.1 新增 UEFI 启动蓝图文档，说明当前 BIOS 路径、未来 UEFI 路径和统一 kernel handoff 的关系。
- [x] 1.2 在蓝图文档中明确本阶段非目标：不实现 `BOOTX64.EFI`、不改变 `make boot-debug`、不替换 MBR/DBR/exDBR/`boot.bin`。
- [x] 1.3 在蓝图文档中记录 x86_64、higher-half ELF64 kernel、long mode、分页、栈、中断关闭和 `BootInfo` 可用性等入口假设。
- [x] 1.4 在蓝图文档中记录现有 BIOS 固定地址布局在本阶段保持不变，并列出未来 handoff ABI 变更需要同步评审的地址假设。

## 2. BootInfo 与 handoff 规划

- [x] 2.1 起草 `BootInfo` 后续版本规划，明确长期采用 `BootInfoHeader + tagged sections`，并覆盖 boot protocol、统一 memory map、framebuffer、firmware tables、loader metadata 和 ABI 兼容策略。
- [x] 2.2 记录下一阶段先定义 header 和少量固定 core 字段，再逐步把 memory map、framebuffer 等放进 tagged sections 的分阶段方案。
- [x] 2.3 明确 BIOS backend 与未来 UEFI backend 都必须作为统一 handoff producer，kernel consumer 不直接消费固件原始协议。
- [x] 2.4 明确未来 kernel entry ABI 直接改为寄存器传递 `BootInfo*`，固定低地址 handoff 仅作为 Legacy BIOS 迁移期 fallback。
- [x] 2.5 明确 `BootInfo` 后续变更必须包含 magic、version、size、alignment、field offset、section 边界和 legacy fallback 的验证要求。

## 3. 内存图兼容规划

- [x] 3.1 起草统一 `BootMemoryRegion` 规划，至少覆盖 physical base、length、normalized type 和 attributes。
- [x] 3.2 记录 BIOS E820 type 到统一 memory region type 的初步映射方向，并说明当前不改变 BIOS 运行时代码。
- [x] 3.3 记录 UEFI `GetMemoryMap` descriptor 到统一 memory region type/attributes 的初步映射方向，并标注为后续 UEFI loader 设计输入。
- [x] 3.4 明确近期不支持调用 UEFI Runtime Services，但统一 memory map 必须保留 runtime memory 类型和 attributes。
- [x] 3.5 为后续内存模块迁移列出独立 change 候选项：从 E820/legacy alias fallback 迁移到统一 memory map consumer。

## 4. 调试入口与镜像规划

- [x] 4.1 在项目级规划中明确 `make boot-debug` 继续代表 Legacy BIOS/MBR/exFAT/Bochs 调试路径。
- [x] 4.2 规划未来独立 UEFI 调试入口名称，例如 `make uefi-boot-debug` 或等价 wrapper。
- [x] 4.3 规划未来 UEFI 镜像产物隔离策略，区分 BIOS raw exFAT image 与 UEFI ESP/FAT image。
- [x] 4.4 规划未来 UEFI smoke test 正式首选 QEMU + OVMF，并将 Bochs UEFI 支持记录为可选验证路径；Legacy BIOS 继续使用 Bochs。

## 5. 后续阶段项目级路线图

- [x] 5.1 创建项目级路线图章节，列出后续unified boot handoff capability：`BootInfoHeader + tagged sections`、寄存器传递 `BootInfo*` 和统一 handoff header 设计与文档化。
- [x] 5.2 创建项目级路线图章节，列出后续TTY console input capability：内存模块迁移到统一 `BootMemoryRegion` consumer，并保留 BIOS fallback。
- [x] 5.3 创建项目级路线图章节，列出后续kernel memory API capability：最小 UEFI loader spike，单独实现 UEFI ELF reader，目标仅为加载 kernel、填充 handoff、进入 `kernel()`。
- [x] 5.4 创建项目级路线图章节，列出后续kernel thread scheduler capability：ESP/FAT 镜像生成、OVMF/QEMU 调试入口和文档化命令。
- [x] 5.5 创建项目级路线图章节，列出后续ring0 syscall diagnostic capability：GOP framebuffer、ACPI RSDP/SMBIOS handoff 和更完整的 UEFI 验证策略。
- [x] 5.6 创建项目级路线图章节，记录 BIOS 与 UEFI 共享 ELF64 加载规则规范，但不要求近期共享 loader 代码。
- [x] 5.7 对每个后续阶段标注当前不实现、推荐前置条件、主要风险和对应 OpenSpec change 候选名称。

## 6. 文档与现有规范同步

- [x] 6.1 更新启动架构相关文档索引或 README 指向 UEFI 蓝图文档，但不把 UEFI 描述为当前可运行路径。
- [x] 6.2 更新现有 BIOS boot layout 文档，补充其作为 Legacy backend 和统一 handoff producer 的定位。
- [x] 6.3 更新 boot debug 文档，明确 `make boot-debug` 的 Legacy BIOS 语义以及未来 UEFI 调试入口的独立规划。
- [x] 6.4 核对 `openspec/specs/x86-bootloader-hardening` 和 `openspec/specs/one-command-boot-debug` 归档后的新要求没有改变当前运行时代码语义。

## 7. 验证与评审

- [x] 7.1 运行 OpenSpec 校验，确认 proposal、design、tasks 和三个 spec delta 可解析。
- [x] 7.2 进行文档一致性检查，确认蓝图没有声称 UEFI 已实现或 `make boot-debug` 已切换语义。
- [x] 7.3 进行 boot ABI/address assumption review，确认本 change 未修改 `boot.s`、`boot.cc`、`BootInfo` header、`link.lds`、kernel entry 或现有启动地址。
- [x] 7.4 记录验证结果：已通过的检查、未运行的运行时检查及原因、当前 change 不涉及 C++/Python 运行时代码修改的说明。

## 验证记录

- OpenSpec：`openspec validate define-uefi-boot-blueprint --strict` 通过，change 可解析。
- 文档一致性：检查 `docs/**/*.md` 中 UEFI 当前可运行、`make boot-debug` 切换 UEFI、`BOOTX64.EFI` 已实现等误导性表述，未发现匹配项。
- ABI/address review：`git status --short` 显示本次运行时代码未修改；改动限定为 README、架构文档和本 change artifacts，未修改 `boot.s`、`boot.cc`、`include/arch/x86/boot/boot_info.h`、`link.lds`、kernel entry 或现有启动地址。
- 运行时检查：未运行 build、Bochs 或 QEMU；本 change 为文档和 OpenSpec 蓝图，不涉及 C++/Python 运行时代码修改。
