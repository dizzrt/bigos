## Context

BigOS 当前块 I/O 请求层已经具备有界请求描述、按设备队列、completion token、pending wait 和 IRQ-safe `complete_from_irq`。但同步提交路径仍在 `submit_sync` 内直接调用 `BlockDevice` 的读写函数，当前 ATA PIO 后端也在读写实现内用端口状态轮询完成数据阶段和 flush。这意味着 page/buffer cache、persistent `/rw`、VFS 文件读写和默认启动路径虽然已经通过请求层提交 I/O，实际完成来源仍是同步轮询后端。

本变更跨越块请求层、块设备接口、ATA PIO 驱动、IRQ/completion 边界、scheduler wait queue 以及 cache/writeback 消费路径。设计目标是改变既有块路径的完成来源，而不是改变调用者契约：cache 和文件系统仍可以调用同步 wrapper 并获得最终状态，但 wrapper 内部必须经由有界 issue/pending/completion/wait 流程，不再依赖设备读写函数在提交调用栈内轮询到最终状态。

本变更不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI 或文件系统格式。默认运行目标仍是 x86_64 Legacy BIOS/MBR/exFAT，QEMU/Bochs Legacy IDE/ATA 是主要验证环境。

## Goals / Non-Goals

**Goals:**

- 把现有块请求同步 API 改成非轮询完成模型上的可阻塞 wrapper，保持调用者仍获得最终 success/error/timeout 状态。
- 为块设备接口增加有界 issue/completion 能力，使设备路径能先提交硬件工作，再由 IRQ 或等价完成源完成请求。
- 迁移当前 ATA PIO 块路径，避免请求层消费者继续依赖同步轮询读写函数作为完成来源。
- 保持 page/buffer cache 的 read miss、dirty writeback、eviction、`sync`/`fsync` 和 persistent `/rw` clean-sync 行为不变。
- 保持 IRQ/completion 路径 allocation-free、nonblocking、EOI-owner-neutral，并防止迟到完成污染复用队列槽。
- 提供默认关闭验证，覆盖成功读写、cache/writeback、timeout/error、迟到完成和默认启动回归。

**Non-Goals:**

- 不实现 virtio、AHCI/SATA、NVMe、DMA、scatter-gather、多队列调度或完整 I/O scheduler。
- 不新增用户态 async I/O syscall、poll/select/epoll、事件 fd、callback ABI 或 POSIX AIO。
- 不引入后台 writeback worker，不改变 dirty block 何时被显式同步或淘汰回写。
- 不改变 boot disk discovery、MBR/exFAT/bigfs 布局、persistent test disk 约定或默认启动设备。
- 不要求 UEFI storage parity、非 x86 后端、完整 APIC 外部 IRQ 重构或网络栈。

## Decisions

1. 同步 API 保留为 request-layer wrapper，而不是暴露新的强制 async 调用模型。
   - 原因：现有 cache、VFS 和文件系统路径依赖同步返回的状态码；本变更只替换完成机制，不扩大到广义异步 I/O 生命周期。
   - 备选：直接新增 only-async API 并改写所有消费者。该方案会把 cache、writeback、VFS 和用户态语义同时纳入，超出本变更边界。

2. 块设备接口采用有界 issue/completion 边界，设备不能把最终完成隐藏在同步轮询回调中。
   - 原因：请求层需要统一队列槽、generation、completion token、timeout 和状态传播；设备只应负责发起硬件命令、传输必要数据阶段，并在完成源可用时完成请求。
   - 备选：保留 `read_impl`/`write_impl` 作为默认完成路径，只在新设备上使用 completion。该方案不能满足“既有块路径迁出同步轮询”的目标。

3. ATA PIO 迁移分阶段保持有界语义：命令发起和数据传输仍显式使用端口 I/O，但等待完成必须通过 IRQ 或等价完成源驱动 request completion。
   - 原因：ATA PIO 数据端口访问仍是 PIO，不等于 DMA；关键边界是不能让上层请求等待绑定在 busy-loop polling 上。实现可以在驱动内部保留短的硬件稳定性检查，但不得把整个请求完成建模为无限或大循环轮询。
   - 备选：一次性引入 DMA 或现代设备。该方案会改变硬件模型和验证面，属于后续现代存储驱动范围。

4. completion 状态仍由请求层拥有，设备 IRQ 只持有 token 或可验证请求身份。
   - 原因：请求层已经具备防止 slot reuse 污染的 generation 机制。继续以请求层为状态源可以保持 timeout、cancel、late completion 和 wakeup exactly-once 的一致性。
   - 备选：设备私有队列独立维护完成状态。该方案会重复请求层状态机，容易让 cache/writeback 看到不一致状态。

5. cache/writeback 语义以“最终状态不变”为迁移验收标准。
   - 原因：M9.2 的用户可见目标不是改变文件系统，而是改变块路径完成方式。cache dirty 状态、writeback failure 保留、eviction 失败、persistent clean-sync 必须继续可解释。
   - 备选：顺带引入后台 writeback 或异步 dirty flush。该方案会改变 durable success 边界，不适合与轮询迁移混在一起。

6. IRQ 所有权不转移给 completion entry。
   - 原因：现有 interrupt dispatch/irqchip 路径拥有 EOI 和向量语义；completion entry 只做状态更新和 wakeup，避免误伤 syscall/exception 或未来 APIC 路径。
   - 备选：由块 completion entry 统一发送 EOI。该方案会把 irqchip 细节泄漏进块层，并破坏现有中断所有权边界。

7. ATA PIO 采用两阶段接入：先用 IRQ-like producer 验证 request-layer completion/wakeup，再接 primary ATA 的 i8259 IRQ14 真实完成路径；多 sector PIO 初版限制为单 sector 或小固定批次完成粒度。
   - 原因：completion token、timeout、迟到完成拒绝和 scheduler wakeup 是请求层问题，ATA IRQ14、PIO 数据阶段和 emulator 中断时序是硬件问题。先用可控 producer 稳定请求层闭环，再接真实 IRQ14，可以把两类问题分开定位。PIO 数据搬运放在 IRQ handler 中会拉长中断路径，因此初版必须限制每次 IRQ 处理的 sector 数，后续再基于验证结果放宽批次。
   - 备选：直接一次性接真实 ATA IRQ 并允许整请求在 IRQ handler 中搬完。该方案会把 completion 状态机、i8259 路由、ATA 时序和 IRQ 最坏执行时间混在一起，失败时难以归因，也更容易影响 timer/scheduler 等其他 IRQ。

## Risks / Trade-offs

- [Risk] ATA PIO 在某些 emulator 上中断时序不稳定，可能导致请求 timeout 或无法可靠进入数据阶段。→ Mitigation：先用 IRQ-like producer 验证 request-layer completion/wakeup，再接 i8259 IRQ14；真实 IRQ 验证同时覆盖 QEMU/Bochs 可用路径，无法运行时明确记录残余风险。
- [Risk] 请求 timeout 后设备 IRQ 迟到，复用队列槽被错误完成。→ Mitigation：继续使用 token 的 request、device、slot、generation 匹配；迟到 completion 必须被拒绝或诊断。
- [Risk] IRQ handler 中执行过多 PIO 数据搬运会拉长中断时间。→ Mitigation：初版把 ATA PIO 完成粒度限制为单 sector 或小固定批次；后续只有在 smoke 耗时和 IRQ 行为可解释后才扩大批次。
- [Risk] 同步 wrapper 仍等待 completion，短期吞吐提升有限。→ Mitigation：本变更的价值是移除 polling 依赖和统一完成模型；吞吐优化和调度策略留给后续异步生命周期能力。
- [Risk] cache/writeback 错误状态在迁移后被重新映射，导致 dirty 状态被错误清除。→ Mitigation：把 cache round-trip、dirty writeback failure、persistent `/rw` clean-sync 纳入验证，并要求状态映射逐项审查。
- [Risk] 新块设备接口膨胀公共头。→ Mitigation：公共头只暴露 request-layer 必需的 issue/completion 契约；ATA 私有状态、端口常量和 IRQ 细节留在驱动内部。

## Migration Plan

1. 梳理 `BlockDevice`、`block_io::Request`、`submit_sync`、ATA PIO read/write、cache load/writeback 和 persistent `/rw` clean-sync 的当前调用链。
2. 扩展块设备接口，加入可中断完成的 issue 操作和能力标志；保留明确的同步兼容边界仅用于不需要轮询的后端或测试后端。
3. 将 `submit_sync` 重构为：验证请求、占用队列槽、arm pending、issue device work、等待 completion/timeout、释放队列槽并返回最终状态。
4. 第一阶段使用 IRQ-like producer 驱动真实 request-layer pending/completion/wakeup 流程，覆盖成功、错误、timeout、重复完成和迟到 completion 拒绝。
5. 第二阶段迁移 ATA PIO 到 i8259 IRQ14：命令发起、设备选择、单 sector 或小固定批次数据阶段和 flush 状态通过有界状态机表达；IRQ14 handler 或等价完成源调用 request-layer completion；错误和 timeout 通过统一状态返回。
6. 更新 cache/writeback 消费路径的验证，不改变其公开语义，只确认底层块请求已经走非轮询完成模型。
7. 增加默认关闭 runtime smoke 和源级检查，覆盖成功、错误、timeout、late completion、cache round-trip、dirty failure、ATA IRQ14 路径和 default boot 回归。
8. 回滚策略：恢复 `submit_sync` 直接同步 dispatch 和 ATA PIO 同步函数，同时保留已独立可用的 completion 模型；回滚不需要改变磁盘镜像、boot handoff、用户态 ABI 或文件系统格式。

## Open Questions

- 暂无。ATA PIO 按“两阶段接入 + i8259 IRQ14 + 单 sector 或小固定批次 PIO 完成粒度”执行；若后续验证显示 emulator 或 IRQ 时序不满足该路径，再以新的设计记录收敛调整。
