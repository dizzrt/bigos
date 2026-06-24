## Context

BigOS 当前块 I/O 已经具备 request-layer queue、completion token、scheduler wait/wakeup、非轮询块路径和 ATA IRQ14 接入基础。这个基础解决了“完成来源不再是提交调用栈内同步轮询”的问题，但仍需要把请求生命周期本身变成明确、可验证、可诊断的内核契约：请求何时占用槽位、何时 armed、issue 失败如何收敛、timeout/cancel 后谁拥有最终状态、迟到 completion 如何拒绝、同步 wrapper 如何传播状态，以及诊断如何在 IRQ-safe/freestanding-safe 边界内保持稳定。

本变更横跨块请求层、completion handoff、scheduler wakeup、ATA PIO 驱动、RAM/smoke producer、page/buffer cache、persistent `/rw` clean-sync 和运行时验证。设计目标不是新增用户可见 async I/O API，而是让内核内部异步块请求生命周期在后续现代存储驱动接入前具备 fail-closed 的状态机与诊断边界。

不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、文件系统格式、用户态 ABI 或默认启动设备选择。默认运行目标仍是 x86_64 Legacy BIOS/MBR/exFAT；QEMU/Bochs Legacy IDE/ATA 仍是主要验证环境。IRQ/completion 路径必须保持 freestanding-safe，不依赖 hosted libc、异常、RTTI、动态分配、用户地址空间或大型局部数组。

## Goals / Non-Goals

**Goals:**

- 定义有界异步请求状态机，覆盖 free、allocated、armed、issued、pending、completing、terminal 和 reusable 等状态及其合法转换。
- 确保每个请求 exactly-once 进入 terminal 状态，并在 timeout/cancel、issue failure、device error、queue full、重复 completion 和迟到 completion 下保持确定性返回值。
- 保持 completion token 的 device、slot、generation 和 request identity 绑定，防止 timeout 后的迟到 IRQ 污染复用槽位。
- 约束 IRQ-safe completion 入口只发布有界状态和唤醒等待者，不执行阻塞、分配、文件系统/cache policy、用户内存访问或 EOI。
- 为非轮询块路径提供可复现诊断，帮助区分 issue failure、device error、timeout、completion rejection、slot reuse 和 writeback failure。
- 保持 page/buffer cache、dirty writeback、`sync`/`fsync`、persistent `/rw` clean-sync 和默认启动行为的用户可见语义不变。

**Non-Goals:**

- 不实现用户态 async I/O syscall、poll/select/epoll、eventfd、callback ABI 或 POSIX AIO。
- 不实现后台 writeback worker、完整 I/O scheduler、多队列调度、公平性策略、吞吐优化或请求合并策略。
- 不实现 virtio、AHCI/SATA、NVMe、DMA、scatter-gather、网络栈、UEFI storage parity 或非 x86 后端。
- 不改变 PIC/LAPIC EOI 所有权、中断向量、exception/syscall dispatch、CR3 切换、page table layout、boot disk discovery 或文件系统格式。
- 不把诊断扩展成无界日志系统；诊断必须是固定容量、可截断、可在低层路径安全使用的状态记录。

## Decisions

1. 请求层拥有生命周期状态机，设备只拥有硬件推进状态。
   - 原因：request slot、generation、completion token、timeout 和同步 wrapper 返回值都由请求层统一维护，设备私有状态不应决定请求是否可复用。
   - 备选：让每个设备维护自己的完整生命周期。该方案会导致 ATA、RAM、未来现代设备重复实现 terminal/timeout/late-completion 规则，并增加 cache/writeback 观察到不一致状态的风险。

2. terminal 状态 exactly-once 发布，后续 completion 只能进入 rejection/diagnostic 路径。
   - 原因：timeout/cancel 与真实 IRQ 可能竞争；只有第一次合法 terminal 转换可以唤醒等待者并决定同步 wrapper 返回值。
   - 备选：允许迟到 completion 覆盖 timeout。该方案会让调用者已经观察到的失败状态被事后改写，破坏 dirty writeback 和 slot reuse 安全。

3. completion token 继续包含可验证身份，拒绝跨设备、跨槽位和 generation 不匹配的完成。
   - 原因：固定队列槽复用是有界实现的基础，generation 是防止迟到 IRQ 完成新请求的关键。
   - 备选：只按 request 指针或设备当前请求匹配。该方案在栈/静态对象复用、设备重入或 future queue depth 扩展时更脆弱。

4. timeout/cancel 是请求层状态转换，不要求 IRQ 路径回滚硬件。
   - 原因：ATA PIO 等低层设备未必支持安全取消；请求层可以 fail-closed 地停止等待并拒绝后续 completion，同时把设备恢复/重置留给驱动的有界错误处理。
   - 备选：要求所有设备实现真实硬件取消。该方案不适合当前 PIO 后端，也会把现代设备语义提前强加给现有路径。

5. 诊断采用固定容量事件/计数和状态快照，不在 IRQ 路径格式化长字符串。
   - 原因：IRQ-safe/freestanding-safe 路径不能分配、不能阻塞，也不能依赖复杂格式化；固定码、计数器和短 marker 更容易在 QEMU/Bochs 串口输出中复现。
   - 备选：在每次异常完成时直接输出详细文本。该方案会拉长中断路径，并可能在用户 CR3 或低层故障期间触发新的诊断问题。

6. 同步 wrapper 仍是现有 cache/VFS 的调用边界，但必须只消费 lifecycle terminal 状态。
   - 原因：本变更不引入用户可见 async I/O；保留同步 wrapper 可以限制变更面，同时确保内部完成模型可被验证。
   - 备选：把 cache/VFS 全部改为异步调用链。该方案会牵涉文件系统、用户 ABI、scheduler policy 和更多状态机，超出本变更边界。

7. 验证先覆盖 request-layer producer，再覆盖真实 ATA 路径和 cache/writeback 回归。
   - 原因：可控 producer 能确定性触发 success/error/timeout/late/repeated completion；真实 ATA 验证再覆盖中断、PIO 数据阶段和 emulator 时序。
   - 备选：只通过默认启动间接验证。该方案无法区分 lifecycle bug、ATA 时序问题和 cache/writeback 回归。

## Risks / Trade-offs

- [Risk] timeout 与真实 IRQ completion 竞争导致状态被重复发布。→ Mitigation：terminal CAS/guard、generation 匹配和 rejection 计数必须覆盖该路径，验证中显式触发迟到 completion。
- [Risk] 诊断过多拉长 IRQ handler 或引入不可重入输出。→ Mitigation：IRQ 路径只更新固定容量状态、短计数或待普通上下文读取的快照；长文本输出只在非 IRQ 验证路径执行。
- [Risk] ATA PIO 设备错误后请求层已 timeout，但硬件仍处于忙或错误状态。→ Mitigation：请求层拒绝迟到 completion，驱动保留有界 reset/settle 诊断边界；验证记录 emulator 相关残余风险。
- [Risk] 同步 wrapper 掩盖内部 lifecycle bug。→ Mitigation：默认关闭 smoke 必须检查 pending、terminal、rejection 和 slot reuse 计数，而不只检查读写最终成功。
- [Risk] cache/writeback failure 被错误映射为成功并清除 dirty。→ Mitigation：writeback failure retention、dirty victim eviction failure 和 persistent clean-sync failure 纳入验证。
- [Risk] 状态枚举和诊断码扩大公共头。→ Mitigation：公共接口只暴露请求层契约所需的稳定状态；设备私有硬件状态和验证辅助保留在实现文件或私有头中。

## Migration Plan

1. 梳理当前 request slot、completion token、queue state、timeout、submit_sync、ATA IRQ14 和 cache/writeback 调用链，记录已有状态转换与缺口。
2. 定义请求生命周期状态、terminal 原因、completion rejection 原因和固定诊断快照，保持结构有界且 freestanding-safe。
3. 加固 request-layer 状态转换：占槽、arm、issue、pending wait、terminal publish、timeout/cancel、late/repeated completion rejection 和 slot release/reuse。
4. 加固 IRQ-safe completion entry：只做 token 匹配、terminal publish/rejection、wakeup 和固定诊断更新，不发送 EOI、不分配、不阻塞、不触发 cache/filesystem policy。
5. 将非轮询块路径和同步 wrapper 的返回值统一映射到 lifecycle terminal reason，保持 cache/writeback 外部语义不变。
6. 增加默认关闭验证：request-layer producer 覆盖成功、错误、timeout、重复/迟到 completion 和 slot reuse；ATA 路径覆盖真实中断完成；cache/writeback 覆盖 dirty failure 保留和 clean-sync。
7. 运行最窄构建、OpenSpec 校验和可用 emulator smoke；如工具链、QEMU、Bochs、ROM/display 或磁盘镜像不可用，验证记录必须列出跳过项和残余风险。
8. 回滚策略：恢复到既有非轮询块路径的较宽松状态机与诊断，同时保留已归档的 request/completion 基础；回滚不改变磁盘镜像、用户 ABI、boot handoff 或文件系统格式。

## Open Questions

- 暂无。首版按固定容量诊断、request-layer lifecycle ownership、IRQ-safe completion handoff 和同步 wrapper 状态传播实现；后续若现代设备需要更深队列或请求合并，以新的设计记录扩展。
