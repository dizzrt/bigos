## Context

BigOS 当前已经具备默认 PID-1 init、`/bin/sh`、简单 `/bin/*` 程序、进程/fd、pipe/dup、信号、时间/身份和 RAM-backed `/rw` 文件行为。已有 runtime smoke 能证明部分路径可启动或发射 marker，但 Stage 26 的重点是把这些路径转化为更稳定的行为断言：验证应说明运行了什么用户可见行为、如何判定成功或失败、环境缺失时哪些检查被跳过、剩余风险是什么。

该 change 影响验证层与用户态可观察行为，不改变 boot handoff、内核链接地址、IDT/syscall vector、CR3 切换、page-table layout、disk layout 或现有 ABI。当前 runnable backend 仍是 x86_64 Legacy BIOS/MBR/exFAT；QEMU headless 是优先自动化路径，Bochs、图形显示、手工键盘输入或更接近硬件的证据保持分层可选。

## Goals / Non-Goals

**Goals:**

- 定义行为导向验证矩阵，把 shell、简单 C 程序、进程/fd、文件系统和用户态兼容性验证统一到可观察结果。
- 要求验证结果可由 runtime 输出、退出状态、文件内容、fd/pipe 端点效果、串口/日志或等价确定性信号判定。
- 保留分层验证：源码/spec 一致性和构建检查可作为基础层，QEMU headless 行为断言作为优先自动化层，Bochs/图形/人工交互作为场景化补充证据。
- 规定环境依赖项缺失时的 skipped/blocked 记录，避免未运行的 emulator 或硬件检查被记为通过。
- 让后续实现任务可以逐步扩展现有 smoke/userland 测试，而不要求一次性建立 release-grade CI。

**Non-Goals:**

- 不新增 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe、新存储驱动或第二可运行 backend。
- 不引入完整 POSIX 进程模型、作业控制、session、terminal process group、完整 shell grammar、动态链接、完整 POSIX libc、SMP、async I/O 或 broad file-backed `mmap`。
- 不把图形显示、Bochs、人工输入或真实硬件验证变成每次变更的强制门禁。
- 不修改 boot 地址、链接地址、interrupt vector、syscall vector `0x80`、page-table 自映射地址、磁盘布局或用户/内核 ABI。

## Decisions

- 优先使用行为矩阵而不是只增加 marker。marker 适合低层启动和 panic 观测，但 Stage 26 的风险来自用户可见行为退化；矩阵需要说明输入、执行路径、预期输出或状态，以及失败判定。
- 将 QEMU headless 作为首选自动化层。该路径最接近现有工具链和串口/日志观测能力，适合保护默认 userland/init、shell 命令、简单 C 程序和文件/fd 行为；Bochs 与图形输入证据保留给硬件行为、显示/键盘交互或早期 boot 风险。
- 复用现有 bounded userland 与 default-off smoke 机制。新增验证应优先组合现有 `/bin/sh`、简单 C 程序、userland smoke 和 helper 脚本，而不是引入 hosted 测试框架或依赖目标 OS 尚未实现的服务。
- 把环境缺失视为显式结果。缺少交叉工具链、QEMU、Bochs、显示/ROM、串口日志或磁盘镜像配置时，验证记录必须说明 skipped/blocked、替代检查和残留风险，不能静默通过。
- 文档同步作为验证可用性的组成部分。行为矩阵和边界需要在 docs/en 与 docs/zh 保持同步，roadmap 只保留规划级描述，不承载具体命令、marker 或文件路径细节。

## Risks / Trade-offs

- [Risk] 行为矩阵过宽导致实现阶段一次性工作量过大 -> Mitigation: 以现有能力为边界，优先覆盖默认 boot/userland、shell、简单 C 程序、进程/fd 和 `/rw` 文件行为的代表性组合，不追求完整 POSIX 笛卡尔积。
- [Risk] emulator 环境在本地或 CI-like 环境不稳定 -> Mitigation: 将源码/spec、构建、QEMU headless、Bochs/图形/人工交互分层记录，并要求缺失环境显式标记 skipped/blocked。
- [Risk] 验证脚本过度依赖文本输出导致脆弱 -> Mitigation: 每个断言优先绑定稳定的输出、退出状态、文件内容或 fd/pipe 端点效果；调试性噪声不得作为唯一成功条件。
- [Risk] 后续 backend 工作误把行为验证等同于跨架构支持 -> Mitigation: spec 明确当前 runnable backend 仍是 x86_64 Legacy BIOS/MBR/exFAT，跨 backend 只要求边界记录，不要求当前实现。
- [Risk] 文档与 OpenSpec 要求漂移 -> Mitigation: 实现任务包含 docs/en 与 docs/zh 同步、OpenSpec 状态检查和针对 roadmap Stage 26 的一致性复核。
