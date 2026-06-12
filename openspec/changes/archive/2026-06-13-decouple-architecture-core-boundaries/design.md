## Context

BigOS 当前可运行 backend 是 x86_64 Legacy BIOS/MBR/exFAT 路径，核心系统已经覆盖启动、内存、IRQ、调度、syscall、进程、VFS、用户态运行时和基础设备驱动。随着用户态和文件系统能力成为基线，后续重构会越来越容易触碰架构机制与核心概念的边界。

当前问题不是缺少第二个 backend，而是核心层在真实消费点容易把 x86_64 事实当作通用内核事实使用。例如异常/IRQ/syscall 入口、页表和地址空间、用户态进入、上下文切换、timer/keyboard/block 设备、启动交接与构建组织都包含硬件常量、汇编约定或 x86 专属对象。该 change 要先建立边界纪律，避免未来在未整理依赖方向的情况下叠加更多功能。

约束和假设：

- 可运行 backend 仍是 x86_64，启动路径仍是 Legacy BIOS/MBR/exFAT。
- 不改变 higher-half kernel 地址、boot handoff、IDT/syscall vector、page-table layout、CR3 切换、disk layout 或用户态 syscall ABI。
- x86_64-elf-gcc、xmake、QEMU/Bochs 仍是当前工具链和验证基线。
- 文档需要保持 `docs/en` canonical 与 `docs/zh` 同步；`roadmap.md` 只保留规划级表述。

## Goals / Non-Goals

**Goals:**

- 定义核心层与 x86_64 backend 的依赖方向：核心层消费稳定架构边界接口，x86_64 机制由 backend 或驱动层实现。
- 在真实消费点整理架构边界，优先覆盖启动交接、IRQ/syscall、调度、内存、用户态进入和 x86 设备驱动。
- 保留低层代码显式性，避免用泛化 abstraction 隐藏硬件约束或改变现有 ABI。
- 让文档、OpenSpec 和代码审查都能识别“当前 x86_64 事实”与“核心抽象契约”的差异。
- 为未来多架构工作降低迁移风险，但不要求现在提供第二个 runnable backend。

**Non-Goals:**

- 不新增 UEFI runnable backend、non-x86 backend、SMP、宽泛设备模型或平台总线模型。
- 不重写启动加载器、页表布局、interrupt vector、syscall ABI、用户态 ABI 或磁盘布局。
- 不把现有 x86_64 兼容路径抽象成完整跨架构 HAL，也不承诺所有核心 subsystem 已经 architecture-neutral。
- 不引入动态链接、完整 POSIX、完整 libc、persistent full writable filesystem 或 async I/O。

## Decisions

1. 采用“真实消费点优先”的边界整理方式。

   - 决策：只在核心层已经消费 x86_64 机制的位置引入或收敛边界，不为了未来 backend 预先设计未被使用的接口。
   - 理由：BigOS 仍是早期 freestanding kernel，过早泛化会增加隐藏控制流和错误抽象。
   - 备选：先设计完整跨架构 HAL。该方案覆盖面更大，但没有第二 backend 校验，容易形成 speculative API。

2. 保持 x86_64 backend 为唯一 runnable backend。

   - 决策：架构解耦完成后，现有 x86_64 Legacy BIOS/MBR/exFAT 路径必须仍是默认可验证路径。
   - 理由：当前 boot、ATA PIO、PIC/PIT、VGA/COM1 和用户态路径都依赖该 backend；稳定性优先于扩展 backend 数量。
   - 备选：同时创建空的 non-x86 backend 目录。该方案会制造没有真实消费和验证的维护负担。

3. 使用窄接口表达核心概念，保留 backend 里的硬件细节。

   - 决策：核心层可依赖的概念应是中断分发、时间 tick、地址空间切换、用户态进入、上下文切换、早期启动信息、port/device IO 的最小契约；x86_64 特有常量、寄存器布局、GDT/TSS/IDT 细节、CR3 操作和汇编入口应留在 x86_64 实现侧。
   - 理由：核心代码需要可读且 freestanding-safe，但不应直接散落硬件细节。
   - 备选：仅靠目录移动表达解耦。目录移动不能阻止 include 泄漏、ABI 误用或文档误承诺。

4. 不改变现有地址、ABI 和 failure behavior。

   - 决策：本 change 的实现不得改变 boot/linker 地址、IDT/syscall vector、page table self-mapping、direct map、disk offset、CR3 切换语义、syscall ABI、异常/IRQ EOI 语义或 panic marker。
   - 理由：这些是现有 bootability 和 runtime smoke 的关键假设。
   - 备选：趁解耦同步重排 layout。该方案风险高，且会把边界纪律与功能迁移混在一起。

## Risks / Trade-offs

- [Risk] 抽象层过宽导致隐藏硬件关键细节 -> Mitigation: 每个边界接口必须有当前 x86_64 调用方/实现方，且在文档中标明非目标。
- [Risk] 核心层继续 include x86_64 私有头或依赖裸常量 -> Mitigation: 任务中加入 targeted include/dependency 搜索和 review checklist。
- [Risk] 目录或命名调整破坏启动/IRQ/syscall/上下文切换 ABI -> Mitigation: 禁止改变地址、vector、frame layout 和 syscall ABI，并用现有构建或 emulator smoke 验证。
- [Risk] 文档把“未来多架构准备”误写成“已有多架构支持” -> Mitigation: 英中同步文档必须明确唯一 runnable backend 仍是 x86_64。
- [Risk] 环境缺少 QEMU/Bochs 或 cross toolchain -> Mitigation: 记录缺失工具、跳过的 runtime 验证和剩余风险；至少执行 OpenSpec 状态检查和静态一致性搜索。

## Migration Plan

1. 盘点真实消费点，区分核心概念、x86_64 backend 实现和设备驱动职责。
2. 为必要消费点引入最小边界接口或收敛现有 include，保持调用路径和 ABI 不变。
3. 调整文档与规格，明确该阶段只建立 x86_64/core 解耦纪律。
4. 执行最窄可用构建检查；若涉及 boot、IRQ、memory、driver 或 syscall 路径，优先运行 QEMU headless smoke，必要时补充 Bochs/QEMU 交叉验证。
5. 若发现变更破坏 bootability 或 runtime marker，回退具体边界收敛改动，保留不改变 ABI 的文档和规格部分继续迭代。

## Open Questions

- 哪些现有 x86_64 头文件需要成为 public arch boundary，哪些应保持 backend-private，需要在实现盘点后逐项确认。
- 设备驱动与架构 backend 的边界是否需要单独能力描述，取决于实际收敛范围是否超出 PIC/PIT/VGA/ATA 当前路径。
- 是否需要在后续 Stage 引入行为导向验证来覆盖这些边界，应该由独立 change 处理，避免与本 change 混合。
