## Why

当前内核核心与 x86_64 后端机制已经在启动、IRQ、内存、调度、syscall、进程和驱动路径中形成真实耦合；在继续扩展可用系统之前，需要把“哪些是核心概念、哪些是 x86_64 机制”变成主动维护纪律，降低后续重构和未来多架构工作的风险。

现在推进该 change 的目标不是引入第二个可运行 backend，而是在已有 x86_64 Legacy BIOS/MBR/exFAT 路径保持可运行的前提下，把真实消费点的架构边界、依赖方向和验证要求固定下来。

## What Changes

- 建立 `architecture-core-boundary-discipline` 能力，用于描述内核核心与 x86_64 后端之间的边界、依赖方向、命名约束和验证要求。
- 在真实消费点收敛 x86_64 专属机制的暴露方式，避免核心层直接依赖裸硬件细节、汇编入口细节或 x86 专属常量。
- 保留当前 x86_64 runnable backend、Legacy BIOS/MBR/exFAT 启动和现有存储路径，不新增 speculative 的第二个可运行 backend。
- 明确架构解耦工作的非目标：不引入 UEFI 可运行实现、SMP、宽泛设备模型、跨架构 ABI 承诺、动态链接或完整 POSIX 平台承诺。
- 要求相关文档和规格把“当前仅 x86_64 支持”和“可被核心消费的抽象概念”分开表述，避免把阶段性 x86_64 事实误写成通用内核契约。

## Capabilities

### New Capabilities

- `architecture-core-boundary-discipline`: 定义内核核心与 x86_64 backend 的边界纪律，覆盖真实消费点的依赖方向、接口形态、文档表述、非目标和验证要求。

### Modified Capabilities

- 无。该 change 新增边界纪律能力，不改变现有运行时行为规格的外部可见需求；若实现过程中发现必须修改已有规格，应在后续 delta 中单独声明。

## Impact

- 影响子系统：启动交接、异常/IRQ/syscall 入口、调度上下文切换、内存管理页表/地址空间、进程用户态进入、计时器/键盘/块设备驱动、构建组织和架构相关 public/private headers。
- 影响代码方向：核心层应消费稳定的架构边界接口；x86_64 专属实现继续位于架构后端或驱动层，且不得通过随意 include 或常量泄漏扩大核心层耦合。
- 影响文档方向：`docs/en` 与 `docs/zh` 需要同步说明该阶段只整理边界纪律，不承诺 runnable non-x86 backend；`roadmap.md` 保持规划级表述，不加入具体文件、命令或验证 marker。
- 工具链假设：继续使用 x86_64-elf-gcc、xmake、QEMU/Bochs 的现有 Legacy BIOS 路径；验证优先采用最窄可用构建和必要的 headless emulator smoke。
- 内存/ABI 假设：不改变 higher-half kernel 地址、boot handoff、IDT/syscall vector、page-table layout、CR3 切换、disk layout 或用户态 syscall ABI。
