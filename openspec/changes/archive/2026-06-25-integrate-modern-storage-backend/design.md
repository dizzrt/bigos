## Context

BigOS 当前默认可运行基线仍是 x86_64 Legacy BIOS、ATA、exFAT boot assets 与 bounded userland。现代块存储驱动已经定义为一个内核内部发布的 modern-only 块设备后端，并通过请求层 completion 模型返回成功、timeout 或设备错误；但是块层、page/buffer cache 和持久 clean-sync `/rw` 还缺少对该后端的集成选择、同步写回和验证边界。

本设计影响设备/驱动框架、块 I/O 请求层、page/buffer cache、持久 `/rw` 后端以及默认关闭 smoke 验证。它不改变启动地址、链接地址、页表布局、中断向量、syscall ABI、boot disk/exFAT 布局或默认 ATA/userland baseline。

数据流保持单一入口：

1. 现代存储驱动通过设备框架发布内核内部块角色。
2. 块层或验证路径显式选择该角色，构造普通块 I/O 请求。
3. 请求层完成校验、队列绑定、issue、等待和 terminal status 归一化。
4. page/buffer cache 只通过请求层同步 wrapper 读装入或写回。
5. 持久 `/rw` 只在显式选择现代后端的验证或配置路径中把该设备作为 backing store，并继续以 clean-sync 语义定义持久成功。

## Goals / Non-Goals

**Goals:**
- 让现代块存储后端可被块请求层、cache 和持久 writeback 路径显式选择。
- 保持现有有界同步 wrapper 语义：调用者在返回前观察最终 success、timeout 或确定性失败。
- 让 cache load、dirty writeback、eviction writeback 和 device-scoped sync 在现代后端上保持与 ATA/RAM 后端一致的 dirty/failure 语义。
- 让默认关闭验证覆盖现代后端经块层、cache、writeback 的读写往返与错误传播。
- 保持默认启动、默认 `/rw` 策略和 userland baseline 不依赖现代后端。

**Non-Goals:**
- 不引入用户可见 async I/O、设备节点、动态 mount ABI 或新的 syscall。
- 不实现 NVMe、legacy/transitional virtio、packed ring、多队列调度、热插拔或完整 PCI 设备管理。
- 不改变 boot/exFAT/ATA 默认路径，不把现代后端设为默认启动盘。
- 不声明 crash consistency、journal replay、power-loss recovery 或完整 POSIX 文件系统语义。
- 不把 IRQ completion 扩展为 cache eviction、filesystem sync 或后台 writeback 执行点。

## Decisions

1. 现代后端通过显式块角色选择接入，而不是替换默认块设备。

   理由：默认 ATA/exFAT/userland baseline 是当前可运行基线，现代后端需要先通过默认关闭验证建立信任。显式选择可以避免启动路径、磁盘布局和用户 ABI 被偶然改变。

   替代方案：把现代后端作为默认块设备优先级最高的设备。该方案会把驱动稳定性风险扩大到默认启动和 `/rw`，并且会让环境缺少现代设备时的行为更难诊断。

2. cache 和 `/rw` 不直接调用现代驱动私有接口，只使用现有块请求层。

   理由：请求层已经承担上下文边界、队列身份、completion generation、terminal status 和同步等待。复用它可以让 ATA、RAM 和现代后端共享同一 failure contract。

   替代方案：为现代后端增加 cache 专用 fast path。该方案会绕过既有请求层语义，增加 dirty 状态和 completion 状态不一致的风险。

3. durable success 只在 request-layer terminal success 后发布。

   理由：现代后端 completion 可能经 IRQ 迟到、timeout 或设备错误返回。cache 清 dirty、`fsync` 成功、eviction 复用和 clean reboot 验证都必须以最终成功为准，不能把 pending 或设备私有状态误判为成功。

   替代方案：提交 write 请求后即认为 dirty block 可清理。该方案会在 timeout、queue rejection 或设备错误时丢失 dirty 数据。

4. IRQ/completion 路径只完成请求，不执行 cache 或 filesystem policy。

   理由：completion 入口必须 allocation-free、nonblocking 且 IRQ-safe。cache eviction、dirty scanning、metadata commit 和 `/rw` sync 都可能分配、阻塞或访问更高层状态，必须留在可阻塞上下文。

   替代方案：在 completion 中继续推进写回队列。该方案会把文件系统策略放入 IRQ 上下文，并破坏当前调度与内存分配边界。

5. 验证采用默认关闭、显式后端选择，并保留默认启动回归。

   理由：现代后端依赖仿真器设备配置和工具链能力。验证必须在可用环境中覆盖集成路径，在不可用环境中记录 skipped/blocked，而不是声明通过。

   替代方案：把现代后端集成 smoke 并入默认启动。该方案会让缺少设备支持的开发环境无法运行默认 baseline。

## Risks / Trade-offs

- [Risk] 现代后端与 ATA/RAM 后端的 sector/block size、容量边界或只读属性不一致，导致 cache key 或范围校验错误。→ Mitigation: 在请求层统一校验设备容量、sector count、buffer length 和 overflow，并在验证中覆盖边界失败。
- [Risk] timeout 后迟到 completion 污染复用队列槽，使 cache 或 `/rw` 观察到错误成功。→ Mitigation: 继续依赖请求层 device identity、queue slot 和 generation 绑定，现代后端 completion 必须通过统一 completion entry。
- [Risk] cache 在 writeback 失败后错误清 dirty 或复用 dirty victim。→ Mitigation: cache 只在 terminal success 后清 dirty；queue full、issue failure、timeout、device error 和 completion rejection 都保持 dirty 或 pending 状态。
- [Risk] 持久 `/rw` 在现代后端验证中被误描述为默认支持或 crash-safe。→ Mitigation: 文档与验证记录明确限定为显式选择、clean-sync、default-off，不声称 crash recovery 或默认后端替换。
- [Risk] 仿真器缺少现代存储设备或 MSI-X 配置导致运行时验证不可执行。→ Mitigation: 验证脚本记录工具链、仿真器、磁盘镜像和设备配置缺失项，并保留源级检查和默认启动回归作为替代证据。

## Migration Plan

1. 保持默认启动和默认 `/rw` 后端策略不变，新增现代后端选择路径只被默认关闭验证或显式内核配置使用。
2. 先让请求层接受并诊断现代后端请求，再接入 cache load/writeback，最后接入持久 `/rw` clean-sync 验证。
3. 每一步都保留 ATA baseline 回归；如果现代后端不可用或失败，默认启动仍回到既有路径。
4. 回滚策略是关闭现代后端集成验证或显式选择入口，保留已发布但未被默认路径依赖的现代驱动。

## Open Questions

- 无。首版明确采用显式选择、默认关闭验证和 clean-sync 边界；后续是否把现代后端纳入默认启动或通用 mount 策略应作为独立 change 决策。
