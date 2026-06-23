# 架构与核心边界

BigOS 当前只有一个默认具备 bounded userland baseline 内 runtime parity 的架构/backend
路径：通过 UEFI ESP/FAT 与 QEMU/OVMF 启动的 x86_64。Legacy BIOS/MBR/exFAT
路径仍作为显式可运行兼容 backend 保留，用于低层 BIOS、ATA、port-IO 和 Bochs 验证。
架构边界整理需要清晰保留两条路径，并明确区分 kernel core 概念与 x86_64 机制。本工作
不新增 Secure Boot、超出有界 framebuffer text console 的宽泛图形栈、ACPI handoff、
UEFI Runtime Services、non-x86 backend、宽泛设备模型、动态链接或完整 POSIX 覆盖。

## 边界规则

核心代码应命名它消费的概念，x86_64 backend 或设备代码应拥有实现该概念的机制。

- Boot handoff consumer 可以接收规范化 boot-info 指针，但 Legacy BIOS 固定地址、
  E820 布局细节、loader 页表保留区和 boot-sector 机制留在 x86 boot 路径。
- Interrupt 和 syscall consumer 可以通过当前 interrupt API 路由 exception、IRQ
  和 `int 0x80` dispatch，但 IDT descriptor、vector table 安装、CR2 读取、保存寄存器
  frame layout 和 EOI 规则仍是 x86_64/i8259 实现细节。
- Scheduler 和 process 代码可以保存 scheduler stack pointer、请求地址空间切换、
  进入或恢复 user context，并消费窄 `bigos::arch_context` 语义边界，但 AMD64
  callee-saved switch frame、`iretq` entry frame、GDT selector 和 TSS `rsp0`
  细节留在 x86_64 实现内。
- VM/user-entry consumer 可以通过窄
  `include/bigos/arch_vm_user_boundary.h` 边界查询 active root、请求地址空间
  activation、分类 user-return frame、捕获 user resume frame 并进入 ring3。
  CR3 mask/write、TLB effect、user selector、TSS state 和 `iretq` frame 细节
  留在 architecture-owned code 内。
- Memory management 可以暴露按页数分配 kernel page、user-root 操作和地址空间激活，
  但 4-level PML4 布局、recursive self-mapping、direct-map 常量、PTE bit position、
  `invlpg` 和 CR3 指令属于 x86_64 paging 细节。
- 设备驱动拥有 VGA text memory、COM1 serial、i8259 PIC、PIT、CMOS RTC、
  keyboard scancode 和 ATA PIO port 等 PC 硬件常量；核心代码不应在窄 driver-facing
  路径之外重复这些常量。

## 当前消费点

当前代码库在真实 runtime seam 上仍有有意保留的 x86_64 耦合：

- `kernel/arch/x86/boot` 拥有 BIOS boot sector、long-mode transition、早期页表、
  ATA/exFAT 加载，以及 boot-info 数据的具体生产者侧。
- `kernel/core/irq` 拥有当前 IDT setup、ISR stub、x86 exception state、IRQ dispatch、
  syscall vector dispatch 和 i8259 EOI 分流。
- `kernel/core/sched` 拥有单核 scheduler 策略，而 assembly context-switch frame
  仍是 AMD64 ABI 细节。scheduler policy 通过 `include/bigos/arch_context.h`
  边界消费 IRQ-return context eligibility 和 kernel context switch，而不是
  open-code raw frame offset 或 assembly symbol。
- `kernel/mm` 拥有当前 x86_64 page-table 操作和 CR3 activation，因为尚不存在
  alternate paging backend。核心 process policy 仍必须把 VMA/process metadata
  作为授权来源，把 page table 作为已经 materialized 的状态。
- `kernel/core/proc` 拥有 process lifecycle、VMA policy、safe teardown 和有界
  user-fault lifecycle。它通过 `include/bigos/arch_vm_user_boundary.h` 消费
  address-space activation、TSS `rsp0`、user entry、user-return classification
  和 fork resume-frame capture；更低层的 `include/bigos/user_mode.h` 与 entry
  assembly 仍是 x86_64 implementation detail。
- `kernel/drivers` 以及 driver-facing core 路径拥有 VGA、serial、PIC、PIT、CMOS RTC、
  keyboard 和 ATA PIO 的 legacy PC device access。

这些是当前事实，不代表 kernel core 今天已经 architecture-neutral。

## 保持不变的假设

除非单独 change 声明并验证行为变化，架构边界 cleanup 必须保持以下假设：

- `docs/zh/arch/x86-boot-layout.md` 记录的 higher-half kernel base、Legacy BIOS
  固定 handoff 地址、boot-info ABI 和 linker entry 假设。
- IDT vector、exception/IRQ/syscall dispatch 分流，以及 syscall 路径不发送
  i8259 EOI 的规则。
- fork、signal、syscall、IRQ-return preemption 和 context switching 消费的
  interrupt frame 与 scheduler context-switch frame layout。
- `include/bigos/arch_context.h` 边界只是当前 x86_64 backend 的 core-facing
  contract；它不承诺完整 HAL、SMP、UEFI runtime parity、non-x86 runtime
  parity、APIC/IOAPIC 支持或 HPET 支持。
- x86_64 page-table layout、recursive self-mapping window、direct map window、
  CR3 root 语义和 TLB invalidation 行为。
- 共享到 user root 的现有 higher-half kernel mapping、user low-half 隔离、
  KVMEM/direct-map 可用性、user stack 假设和当前 CR3 switching 语义。
- 默认 UEFI ESP/FAT boot packaging、显式 Legacy BIOS/MBR/exFAT boot packaging，
    以及当前 ATA PIO/exFAT runtime storage 兼容路径。
- 最小 syscall ABI：syscall number 与参数仍使用现有 x86_64 寄存器，结果返回
  `rax`，并保留有界 user-buffer validation。

## SMP 准备边界

当前 SMP-ready 边界包含 AP startup、per-CPU local timer state 与 bounded per-CPU
scheduler domain。CPU-local accessor 可以描述每个已初始化为 schedulable 的 online
CPU 的当前线程、当前进程、active address-space root、IRQ 可见 nesting、
preemption-disable depth 和 pending reschedule state。

- Scheduler ownership 显式归属到每个 CPU：ready queue、wait queue、sleep list、idle
  thread、terminated list 和 reschedule intent 属于 owning scheduler domain。初始
  placement 与 wakeup 可以通过 scheduler boundary 向远端 online CPU enqueue work，
  并按先发布 runnable state、再设置 reschedule pending、最后发送 scheduler nudge 的顺序执行。
- IRQ routing 使用显式 vector ownership。CPU exception 与 `int 0x80` 不发送 irqchip
  EOI，PIC fallback IRQ 发送 i8259 EOI，APIC-owned local timer、IPI 和已支持的
  IOAPIC external IRQ 发送 LAPIC EOI。APIC default delivery active 时，scheduler
  tick 归 LAPIC timer，keyboard/input IRQ 通过 IOAPIC 路由到已初始化且 online 的 BSP。
- TLB invalidation 通过携带 address-space root、virtual page 或 range、target CPU
  mask 和 completion requirement 的边界表达。单核实现只接受 bootstrap CPU target，
  并通过本地 `invlpg` 或 CR3 reload 完成。
- Shared scheduler state、IRQ-visible state 和 page-table update 在对 handler、fault
  path 或未来 remote CPU 可见前，必须通过 interrupt-disabled section 或所选本地边界
  完成发布。
- CPU hotplug、NUMA、RCU、MSI/MSI-X、广义 IRQ affinity/load balancing，以及
  非 x86_64 interrupt backend parity 仍是后续依赖。

## Review Checklist

触及 `kernel/core`、`kernel/mm`、公开 kernel header 或 x86_64 backend/device path
的变更使用以下 checklist：

- 识别每个依赖属于核心概念、x86_64 backend 机制、设备驱动机制，还是 build/link
  约束。
- 除非接口明确是当前 x86_64 ABI boundary，否则不要把 x86_64 descriptor、
  GDT/TSS/IDT 细节、CR2/CR3 指令、裸 port 常量或 assembly frame layout 暴露到
  generic-looking interface。
- 当 core caller 不需要具体 x86_64 结构字段时，优先使用 opaque handoff pointer
  和表达语义的 helper 名称。
- 避免 speculative HAL、空的 non-x86 backend 目录，或没有当前 caller 与实现的接口。
- 对 IRQ、port I/O、MMIO 或 driver state 改动，评审 interrupt safety、reentrancy、
  hardware access ordering 和确定性 failure behavior。
- 对 allocator、page-table 或 early-memory 改动，评审 initialization phase、
  allocation context、object lifetime、alignment、rollback 和 failure behavior。
- 对 VM/user-entry/fault 改动，确认 VMA/process metadata 先授权 user access，
  再进行 page-table materialization；可恢复 CPL3 fault 只覆盖已实现的 VMA-backed
  demand-zero/COW/stack-growth case；fault/exit/reaper 路径不得在 unsafe return
  path 上释放 active user root、当前 kernel stack 或 process object。

## Validation

只涉及文档或 include direction 的边界变更至少运行 OpenSpec status 检查和 targeted
consistency search。若 runtime refactor 触及 boot、IRQ、timer、scheduler context
switch、memory mapping、syscall、user-mode entry 或 hardware driver，则还应运行
最窄可用 x86_64 cross-build。若本地 QEMU/Bochs 与 cross toolchain 可用，runtime
path 优先运行 QEMU headless smoke；early boot、port I/O 或硬件行为风险较高时考虑
Bochs 或 QEMU/Bochs cross-validation。
