# 架构与核心边界

BigOS 当前只有一个可运行架构/backend 路径：通过 Legacy BIOS/MBR/exFAT 启动的
x86_64。架构边界整理需要保持该路径可运行，并明确区分 kernel core 概念与
x86_64 机制。本工作不新增可运行 UEFI backend、non-x86 backend、SMP、宽泛设备
模型、动态链接或完整 POSIX 覆盖。

## 边界规则

核心代码应命名它消费的概念，x86_64 backend 或设备代码应拥有实现该概念的机制。

- Boot handoff consumer 可以接收规范化 boot-info 指针，但 Legacy BIOS 固定地址、
  E820 布局细节、loader 页表保留区和 boot-sector 机制留在 x86 boot 路径。
- Interrupt 和 syscall consumer 可以通过当前 interrupt API 路由 exception、IRQ
  和 `int 0x80` dispatch，但 IDT descriptor、vector table 安装、CR2 读取、保存寄存器
  frame layout 和 EOI 规则仍是 x86_64/i8259 实现细节。
- Scheduler 和 process 代码可以保存 scheduler stack pointer、请求地址空间切换、
  进入或恢复 user context，但 AMD64 callee-saved switch frame、`iretq` entry frame、
  GDT selector 和 TSS `rsp0` 细节留在 x86_64 实现内。
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
  仍是 AMD64 ABI 细节。
- `kernel/mm` 拥有当前 x86_64 page-table 操作和 CR3 activation，因为尚不存在
  alternate paging backend。
- `kernel/core/proc` 和 `include/bigos/user_mode.h` 消费 user-mode entry、TSS `rsp0`、
  syscall frame、signal frame 和 fork frame clone，用于当前 x86_64 user ABI。
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
- x86_64 page-table layout、recursive self-mapping window、direct map window、
  CR3 root 语义和 TLB invalidation 行为。
- 默认可运行 backend 使用的 raw disk image layout、Legacy BIOS/MBR/exFAT boot
  packaging 和 ATA PIO storage path。
- 最小 syscall ABI：syscall number 与参数仍使用现有 x86_64 寄存器，结果返回
  `rax`，并保留有界 user-buffer validation。

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

## Validation

只涉及文档或 include direction 的边界变更至少运行 OpenSpec status 检查和 targeted
consistency search。若 runtime refactor 触及 boot、IRQ、timer、scheduler context
switch、memory mapping、syscall、user-mode entry 或 hardware driver，则还应运行
最窄可用 x86_64 cross-build。若本地 QEMU/Bochs 与 cross toolchain 可用，runtime
path 优先运行 QEMU headless smoke；early boot、port I/O 或硬件行为风险较高时考虑
Bochs 或 QEMU/Bochs cross-validation。
