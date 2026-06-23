## Context

BigOS 当前块 I/O 请求层已经提供有界请求描述、按设备队列、同步提交和确定性状态传播。请求进入队列后立即调用当前 `BlockDevice` 同步读写接口，完成状态在提交调用栈内产生；请求层明确不声明异步完成、callback、后台 worker 或 scheduler-integrated I/O waiting。

调度器已经提供可由 IRQ handler 调用的有界 `wake_one`/`wake_all`，等待线程通过 wait queue 进入非 runnable 状态，timer tick 可处理 timeout。这个基础足够承载“提交线程阻塞等待，硬件/验证 IRQ 完成请求并唤醒等待者”的最小闭环，但 I/O completion 状态本身还没有独立契约。

本变更只建立块 I/O 中断驱动完成模型。它不把现有 ATA PIO 路径全面迁出同步轮询，也不新增现代存储驱动；这些属于后续能力。实现时必须保留现有同步消费者和 page/buffer cache 行为，避免让默认 boot disk 或 persistent `/rw` 被未成熟异步路径牵动。

同一实现批次顺带整理验证日志输出目录。当前 `tools/boot_debug.py` 使用 `LOG_DIR = PROJECT_ROOT / 'log'`，xmake run target 也显式传入 `log/*.serial.log`，runtime smoke matrix 默认 artifact 和 per-case serial logs 也落在 `log/` 下。该目录名应统一迁移为 `logs/`，并同步测试与双语文档示例；显式传入的 `--serial-log`、`--output`、`--serial-log-dir` 路径仍按用户输入处理。

## Goals / Non-Goals

**Goals:**

- 定义 freestanding-safe、有界、一次性完成的 I/O completion 状态。
- 扩展请求层，使请求可进入 pending 状态，并由 IRQ-safe 完成入口记录最终状态。
- 将完成入口与 scheduler wait queue wakeup 集成，使普通可阻塞内核线程可等待请求完成或超时。
- 保留现有同步提交语义，允许旧消费者继续获得最终成功/失败状态。
- 提供默认关闭验证，覆盖 pending、IRQ-like completion、重复完成拒绝、等待唤醒、超时和错误传播。
- 将 boot/debug、xmake run target 与 runtime smoke 的默认日志输出目录统一迁移到 `logs/`。

**Non-Goals:**

- 不把现有 ATA PIO 默认读写路径从同步轮询迁出；后续块路径迁移变更再处理。
- 不实现 virtio、AHCI/SATA、NVMe、DMA、多队列调度、I/O scheduler、后台 worker 或异步 writeback。
- 不新增用户态 async I/O syscall、事件 fd、poll/select/epoll 或 POSIX AIO 语义。
- 不迁移显式指定的自定义日志路径；用户通过 CLI 参数给出的路径继续原样使用。
- 不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI 或默认启动设备选择。
- 不要求 UEFI runtime parity、非 x86 后端、完整 APIC 外部 IRQ 迁移或 broad networking。

## Decisions

1. completion 作为请求层内的显式状态，不作为独立 heap 对象默认分配。
   - 原因：IRQ 完成路径必须 allocation-free，且当前请求描述已经持有 operation、buffer、range、status 和 bounded bookkeeping。把 completion 嵌入或由调用者显式提供，可以避免 IRQ 中动态分配和生命周期不清。
   - 备选：每个请求动态分配 completion 对象。该方案会扩大失败面，并让 IRQ 完成路径需要处理分配生命周期，不适合当前 freestanding kernel。

2. 请求状态采用单调生命周期：empty/queued/pending/completed/cancelled-or-timeout，完成只允许成功一次。
   - 原因：中断可能与 timeout 或错误路径交错。单调状态让重复完成、迟到完成和取消后的完成都能确定性拒绝或记录为 late completion，而不会二次唤醒或覆盖最终状态。
   - 备选：只用 bool completed。该方案无法区分 pending、timeout、重复完成和 device error，诊断价值不足。

3. IRQ handler 只调用 bounded completion entry，不执行提交、阻塞、cache 操作或文件系统操作。
   - 原因：现有 scheduler wakeup 已允许 IRQ-safe 唤醒，但提交和 cache 仍需要普通可阻塞上下文。把 IRQ 边界限制为“设置最终状态 + 唤醒等待者”可以保持中断路径有界。
   - 备选：在 IRQ handler 中继续派发后续请求或执行 cache writeback。该方案会把复杂 I/O policy 推进中断上下文，容易违反 allocation-free 和 nonblocking 约束。

4. 同步 API 保留，并在异步-capable 请求上通过 wait queue 等待最终状态。
   - 原因：现有 page/buffer cache 和文件系统路径依赖同步返回状态。本变更先改变完成来源，不强迫所有消费者重写为 callback 或 future 模型。
   - 备选：立即只暴露 submit_async/callback。该方案会扩大到 cache、VFS、文件系统迁移和完整请求生命周期范围。

5. 验证优先使用 RAM block 或 fake IRQ-like producer，而不是要求真实 ATA IRQ。
   - 原因：本变更的核心是完成模型和 scheduler wakeup，不是现代硬件驱动。可控 producer 能稳定覆盖 pending、完成、重复完成和 timeout，再由后续变更把具体硬件路径迁入模型。
   - 备选：直接启用 ATA IRQ 完成。该方案需要重新设计 ATA PIO 中断模式和错误恢复，风险更接近块路径迁移和现代存储驱动范围。

6. 默认日志目录统一使用 `logs/`，但不改写用户显式传入的路径。
   - 原因：默认输出目录是工具约定，不应要求用户每次手动指定；但显式 CLI 参数属于调用者选择，自动改写会破坏脚本兼容性。
   - 备选：继续使用 `log/` 并只更新文档。该方案无法解决 xmake wrapper 和 helper 默认行为不一致的问题。

## Risks / Trade-offs

- [Risk] completion 生命周期与栈上同步请求冲突，IRQ 迟到后访问无效请求。→ Mitigation：pending 请求必须在等待完成或显式取消前保持存活；timeout/cancel path 必须让迟到完成走确定性拒绝或诊断，不释放仍可被 IRQ 观察的状态。
- [Risk] IRQ 完成与等待线程 timeout 竞争导致双重唤醒或状态覆盖。→ Mitigation：完成状态更新使用 IRQ-masked 临界区或现有有界锁保护，只有第一个终止状态拥有唤醒权。
- [Risk] 为了兼容同步 API，异步完成被包装成同步等待，短期内看不到吞吐收益。→ Mitigation：这是本变更的刻意边界；后续变更再迁移轮询路径和扩展请求生命周期。
- [Risk] 验证使用 fake producer 可能不足以覆盖真实硬件 IRQ。→ Mitigation：本变更验证完成模型；真实硬件驱动迁移必须在后续变更补充设备级 smoke 和 EOI/IRQ source 验证。
- [Risk] 新状态机可能让 request layer 头文件膨胀。→ Mitigation：公共头只暴露必要状态、提交/等待/完成入口和诊断名称，设备私有细节留在 `.cc` 或驱动私有头内。
- [Risk] 仓库中历史验证记录可能仍包含 `log/` 路径，机械替换会改变历史事实。→ Mitigation：迁移默认工具、测试和当前文档示例；历史验证记录若表示过去运行结果，可保留或在实现时明确区分。

## Migration Plan

1. 梳理现有 `Request`、per-device queue、`submit_sync`、scheduler wait queue 和 block smoke，确认哪些字段可复用、哪些字段需要 completion 状态。
2. 扩展请求描述和状态枚举，加入 pending/completed/cancelled-or-timeout 语义、wait queue 绑定和一次性完成保护。
3. 新增 IRQ-safe completion entry，允许设备或验证 producer 以最终 `Status` 完成 pending 请求并唤醒等待者。
4. 保留 `submit_sync` 对外语义，将可异步完成路径包装为提交后等待；当前默认同步设备可继续直接完成。
5. 增加默认关闭验证，覆盖普通上下文等待、IRQ-like producer 完成、重复完成拒绝、timeout、错误状态传播和不可阻塞上下文拒绝提交。
6. 将 helper 默认 `LOG_DIR`、runtime smoke 默认 artifact/per-case serial log 目录、xmake run target 显式日志路径、测试断言和当前双语文档示例迁移到 `logs/`。
7. 回滚策略：移除新 completion entry 和 pending path，恢复请求层只通过当前同步 dispatch 产生最终状态；日志目录迁移可单独回滚为 `log/` 默认值；两者都不需要改变磁盘镜像、boot handoff 或用户态 ABI。

## Open Questions

- 暂无。真实 ATA IRQ、现代存储设备和广义异步生命周期策略留给后续变更处理。
