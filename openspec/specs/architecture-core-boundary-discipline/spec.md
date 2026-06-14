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

### Requirement: syscall/user ABI 核心消费边界
BigOS core code SHALL consume syscall and user ABI mechanisms through stable semantic boundaries rather than scattering x86_64-private register, descriptor, interrupt-frame, or assembly-entry details across unrelated core subsystems. x86_64 implementation details MAY remain in the architecture backend and low-level entry code, but core-facing interfaces MUST describe the kernel concept being consumed.

#### Scenario: 核心层使用 syscall 语义接口
- **WHEN** `kernel/core` code routes, validates, or documents syscall behavior
- **THEN** the code MUST express syscall number, argument extraction, return value, errno, and user-copy semantics through stable ABI helpers or documented interfaces
- **AND** unrelated core subsystems MUST NOT open-code x86_64 interrupt-frame offsets or descriptor state

#### Scenario: 架构私有细节留在 backend
- **WHEN** syscall entry requires IDT gate setup, TSS/RSP0 state, ring transition mechanics, or assembly frame handling
- **THEN** those details MUST remain in architecture-specific implementation or explicitly named low-level entry code
- **AND** the core contract MUST remain the stable user/kernel syscall semantics rather than the raw x86_64 mechanism

### Requirement: ABI 边界整理不改变低层布局假设
BigOS architecture boundary cleanup for syscall/user ABI SHALL preserve existing boot, linker, interrupt vector, page-table, CR3 switching, user stack, and disk layout assumptions unless a separate change explicitly declares and validates such a behavior change.

#### Scenario: syscall/user ABI cleanup touches low-level paths
- **WHEN** implementation work touches syscall entry, user entry, exec stack setup, or user wrapper behavior
- **THEN** existing vector, register order, return convention, stack layout, CR3 switching semantics, and no-EOI syscall rule MUST remain unchanged
- **AND** any required behavior change to those assumptions MUST be split into a separate OpenSpec change

#### Scenario: 默认 backend 保持可运行
- **WHEN** ABI boundary cleanup is complete
- **THEN** the default runnable backend MUST remain the current x86_64 Legacy BIOS/MBR/exFAT path
- **AND** documentation MUST NOT claim runnable multi-architecture, UEFI, SMP, or runtime-backend parity support

### Requirement: ABI 边界验证匹配触及范围
BigOS SHALL validate syscall/user ABI boundary changes with checks appropriate to the touched layer. Specification-only or documentation-only work MUST at least pass OpenSpec status and targeted consistency searches; source changes to syscall entry, wrappers, crt0, user program build, or public headers MUST run the narrowest useful build or smoke that the local toolchain and emulator environment supports.

#### Scenario: 文档和规格整理
- **WHEN** a change only updates OpenSpec artifacts or documentation for syscall/user ABI boundaries
- **THEN** OpenSpec status checks and targeted consistency searches MUST be recorded
- **AND** QEMU or Bochs runtime smoke MAY be skipped with that scope explicitly stated

#### Scenario: 源码行为整理
- **WHEN** a change updates syscall dispatcher behavior, ABI headers, userland wrappers, crt0, user program build rules, or user-entry control flow
- **THEN** the implementation MUST run the narrowest available build check
- **AND** when the environment supports it, a matching syscall or userland headless QEMU smoke SHOULD be run and recorded

### Requirement: VM/user-entry 边界作为真实架构消费点
BigOS architecture boundary discipline SHALL treat VM policy, address-space activation, user-entry mechanics, and fault handling as real core/architecture consumption points. Core code MAY consume semantic helpers for those concepts, but MUST NOT scatter x86_64-private CR3, descriptor, interrupt-frame, `iretq`, or raw page-table encoding details across unrelated core subsystems.

#### Scenario: 核心层消费 VM/user-entry 语义
- **WHEN** `kernel/core` or public kernel headers route process execution, user entry, address-space activation, user fault handling, or user-memory validation
- **THEN** the code MUST express the needed kernel concept through stable semantic boundaries or documented low-level entry interfaces
- **AND** unrelated core subsystems MUST NOT open-code x86_64 CR3 writes, GDT/TSS state, raw `iretq` frame layout, interrupt-frame offsets, or page-table bit encodings as portable policy

#### Scenario: 架构私有 VM/user-entry 细节留在 backend
- **WHEN** user entry or address-space activation requires CR3 writes, TLB invalidation semantics, GDT/TSS/RSP0 state, segment selectors, assembly frame construction, or x86_64 page-table bit encoding
- **THEN** those details MUST remain in architecture-specific implementation or explicitly named low-level entry code
- **AND** the core-facing contract MUST remain the stable VM/user-entry/fault semantic behavior rather than the raw x86_64 mechanism

#### Scenario: 边界整理不扩大 backend 承诺
- **WHEN** VM/user-entry architecture boundary cleanup is implemented, documented, or validated
- **THEN** the default runnable backend MUST remain the current x86_64 Legacy BIOS/MBR/exFAT path
- **AND** documentation MUST NOT claim runnable multi-architecture support, UEFI runtime parity, SMP support, broad file-backed `mmap`, dynamic linking, or complete POSIX process compatibility as a result of this cleanup
