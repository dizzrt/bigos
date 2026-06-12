## Purpose

定义 BigOS 当前 x86_64-only baseline 下的核心/架构边界纪律，确保核心层通过明确语义接口消费架构能力，同时保留现有可运行 backend 和低层 ABI 假设。

## Requirements

### Requirement: Core Consumes Explicit Architecture Boundaries

内核核心在消费架构相关能力时 SHALL 通过明确的架构边界接口表达核心概念，MUST NOT 在核心层随意依赖 x86_64 私有头、裸寄存器布局、汇编入口细节或硬件常量。

#### Scenario: Reviewing a core subsystem dependency

- **WHEN** 变更触及 `kernel/core`、`kernel/mm` 或公开 kernel headers 中的架构相关调用点
- **THEN** 评审必须能够识别该调用点消费的是稳定核心概念还是 x86_64 私有机制
- **THEN** x86_64 私有机制必须留在架构 backend 或设备驱动实现侧

#### Scenario: Introducing an architecture helper

- **WHEN** 新增 helper 用于地址空间切换、用户态进入、interrupt/syscall dispatch、上下文切换或 early boot handoff
- **THEN** helper 的接口必须描述核心所需语义
- **THEN** x86_64 寄存器、descriptor、CR3、GDT/TSS/IDT 或汇编 frame 细节必须保持在 x86_64 实现内

### Requirement: Decoupling Preserves Existing Runnable Backend

架构解耦工作 SHALL 保持当前 x86_64 Legacy BIOS/MBR/exFAT backend 为唯一 runnable backend，并 MUST NOT 要求 speculative 的第二 backend 才能构建、启动或验证现有系统。

#### Scenario: Building the default backend

- **WHEN** 完成边界整理后执行默认 x86_64 构建
- **THEN** 构建输入和 runnable backend 仍必须指向现有 x86_64 Legacy BIOS 路径
- **THEN** 变更不得要求 UEFI、non-x86 backend、SMP 或新存储驱动参与默认验证

#### Scenario: Documenting backend support

- **WHEN** 文档或规格描述架构解耦结果
- **THEN** 文档必须说明当前可运行 backend 仍是 x86_64
- **THEN** 文档不得宣称 BigOS 已具备 runnable multi-architecture 支持

### Requirement: Architecture Boundary Work Preserves Low-Level ABI Assumptions

架构边界整理 MUST preserve 现有 boot、linker、interrupt、page-table、disk、context-switch 和 syscall ABI 假设，除非单独 change 明确声明并验证这些行为变更。

#### Scenario: Touching boot or memory layout

- **WHEN** 变更触及启动交接、linker layout、kernel higher-half mapping、direct map、page table self-mapping 或 CR3 切换路径
- **THEN** 变更必须保持现有地址和 page-table 语义不变
- **THEN** 如需改变这些语义，必须拆分为独立 change 并声明 bootability 风险

#### Scenario: Touching interrupt or syscall paths

- **WHEN** 变更触及 exception、IRQ、syscall entry、EOI 分发或 interrupt frame 相关路径
- **THEN** 变更必须保持现有 vector、frame layout、syscall ABI 和 syscall 不发送 i8259 EOI 的语义
- **THEN** unsupported kernel faults 仍必须进入现有 diagnostic 或 panic 路径

### Requirement: Documentation Separates Current Facts From Core Contracts

项目文档和 OpenSpec artifacts SHALL 区分当前 x86_64 事实、核心可消费抽象以及未来多架构非目标，并 MUST 使用与实际实现一致的 bounded wording。

#### Scenario: Updating bilingual documentation

- **WHEN** 变更更新 `docs/en` 中与架构边界、启动、IRQ、memory、driver 或 user-mode entry 相关的说明
- **THEN** 对应 `docs/zh` 路径必须同步更新
- **THEN** 英中说明都必须避免把 x86_64-only baseline 描述为完整跨架构能力

#### Scenario: Updating roadmap language

- **WHEN** 变更更新 `roadmap.md`
- **THEN** 内容必须保持项目规划级别
- **THEN** 内容不得加入具体入口点、文件路径、命令、validation marker、源码级实现细节或 archive/version index

### Requirement: Validation Matches Touched Architecture Boundary

架构边界变更 SHALL 使用与触及范围匹配的最窄有效验证；涉及 boot、IRQ、memory、driver、syscall 或 user-mode entry 的变更 MUST 记录构建或 emulator 验证结果，或明确记录无法执行的环境原因。

#### Scenario: Source-only boundary refactor

- **WHEN** 变更只收敛 include、命名、接口边界或文档，并且不改变 runtime control flow
- **THEN** 至少必须执行 OpenSpec 状态检查和 targeted consistency search
- **THEN** 若没有源文件行为变化，可以不要求 QEMU 或 Bochs runtime smoke

#### Scenario: Runtime boundary refactor

- **WHEN** 变更触及 boot、IRQ、timer、scheduler context switch、memory mapping、syscall、user-mode entry 或 hardware driver runtime path
- **THEN** 必须优先执行最窄可用构建检查
- **THEN** 在环境支持时必须执行 QEMU headless smoke，并在早期 boot、port IO 或硬件行为风险较高时考虑 Bochs 或 QEMU/Bochs cross-validation
