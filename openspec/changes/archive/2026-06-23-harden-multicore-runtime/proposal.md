## Why

BigOS 已经具备多核启动、per-CPU 调度、类型化 IPI 与 TLB shootdown 的基础合同，但这些能力需要在并发压力、跨子系统锁顺序、故障超时诊断上形成可回归的加固边界。现在应先把多核运行时变成可被信任的基线，再让后续异步 I/O、更多用户态负载或更复杂内存路径构建在其上。

## What Changes

- 增加多核运行时加固能力，覆盖并发压力/回归验证、共享状态锁顺序审计、确定性诊断与 fail-closed 行为。
- 扩展 per-CPU run queue 调度合同，要求远程唤醒、跨 CPU runnable 发布、AP tick/IRQ-return 调度路径在压力下保持队列唯一性、锁顺序和可诊断失败。
- 扩展 SMP IPI 与 TLB shootdown 合同，要求 IPI 投递、ack 完成、目标 CPU 过滤、shootdown timeout 在压力与故障条件下有可复现验证和 fail-closed 语义。
- 扩展 AP startup/per-CPU timer 合同，要求 AP 上线、timer tick 和 LAPIC/IOAPIC fallback 的故障路径提供确定性诊断，不把失败 CPU 暴露给调度或 shootdown 消费者。
- 明确非目标：不引入 CPU hotplug、NUMA、完整负载均衡、完整 POSIX 并发模型、异步 I/O、广泛设备驱动、UEFI runtime parity 或新的用户 ABI。

## Capabilities

### New Capabilities
- `multicore-runtime-hardening`: 覆盖多核调度、跨核唤醒、IPI 投递、TLB shootdown 完成、共享状态锁顺序、压力验证、故障诊断与 fail-closed 策略的跨子系统加固合同。

### Modified Capabilities
- `per-cpu-run-queues`: 增强多核调度压力验证、远程唤醒发布顺序、run queue 锁顺序与失败诊断要求。
- `smp-ipi-tlb-shootdown`: 增强 IPI 投递/ack、TLB shootdown 完成、timeout/failure 诊断与 fail-closed 验证要求。
- `ap-startup-percpu-timers`: 增强 AP startup、per-CPU timer 与 APIC fallback 的故障隔离和确定性诊断要求。

## Impact

- 受影响子系统：x86_64 SMP/APIC 初始化、per-CPU timer、scheduler、IPI 分发、TLB shootdown、`mm context` 驻留状态、IRQ 入口/EOI 边界、panic/诊断输出、runtime smoke/helper 脚本。
- 架构假设：目标为 x86_64；Legacy BIOS/MBR/exFAT 路径仍是默认可运行基线；UEFI spike 不要求运行时 parity。
- 内存布局假设：不改变 kernel link address、direct map、page-table self-map、AP trampoline 低地址保留、磁盘布局或用户 ABI。
- 仿真假设：优先使用 QEMU headless 进行自动化多核 smoke；Bochs 可用于早期启动、APIC、端口 I/O 或硬件行为交叉验证；缺失工具时必须记录跳过项和残余风险。
- 工具链假设：继续使用 xmake、`x86_64-elf-*` 交叉工具链与 `uv run python ...` 辅助脚本路径。
