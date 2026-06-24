## 1. 现状梳理与生命周期边界

- [x] 1.1 梳理当前块请求层的 request slot、generation、completion token、pending wait、timeout、submit_sync 和 slot reuse 路径，记录已有状态转换与缺口
- [x] 1.2 梳理 ATA PIO 非轮询路径、RAM/smoke producer、scheduler wakeup、page/buffer cache load/writeback 和 persistent `/rw` clean-sync 对请求状态的依赖
- [x] 1.3 明确本变更不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、文件系统格式、用户态 ABI 或默认启动设备选择
- [x] 1.4 记录本地 xmake、x86_64-elf-gcc/x86_64-elf-g++、QEMU、Bochs、ROM/display 和磁盘镜像路径可用性，作为后续验证前置条件

## 2. 请求生命周期状态机与诊断

- [x] 2.1 定义或整理请求生命周期状态、terminal reason、completion rejection reason 和 slot reuse identity，保持结构有界且 freestanding-safe
- [x] 2.2 加固请求分配、arm、issue、pending wait、terminal publish、timeout/cancel、slot release 和 slot reuse 的状态转换，确保 terminal exactly-once
- [x] 2.3 确保 issue failure、queue full、invalid request、device error、timeout、cancel 或等价失效路径都发布确定性 terminal reason，且不会遗留悬空 pending 请求
- [x] 2.4 加固 completion token 的 device、slot、generation 和 request identity 匹配，拒绝跨设备、跨槽位、generation 不匹配、重复和迟到 completion
- [x] 2.5 增加固定容量诊断快照、计数器或短码，覆盖 issue failure、timeout、cancel、device error、late completion、duplicate completion、identity mismatch 和 slot reuse protection

## 3. IRQ-safe completion 边界

- [x] 3.1 审查并加固 IRQ-safe completion entry，确保只执行 token 校验、terminal 发布或拒绝、固定诊断更新和 scheduler wakeup
- [x] 3.2 确认 completion entry 不执行动态分配、释放、阻塞等待、cache/filesystem policy、用户内存访问、长字符串格式化、大型栈对象或 hosted runtime 操作
- [x] 3.3 确认 completion entry 不发送 PIC/LAPIC EOI，不改变 exception、syscall、timer、keyboard 或未来 APIC 路径的中断所有权语义
- [x] 3.4 审查 terminal 发布与 scheduler wakeup 顺序，确保等待线程恢复后能观察完整 terminal reason 和诊断状态

## 4. 非轮询块路径、cache 与 writeback 集成

- [x] 4.1 将 ATA PIO、RAM/smoke producer 和同步 wrapper 的返回路径统一映射到 request-layer lifecycle terminal reason，不绕过 token/completion 身份检查
- [x] 4.2 确保 ATA timeout 后迟到 IRQ 或设备完成只进入 rejection/diagnostic 或驱动恢复路径，不覆盖已发布 terminal 状态
- [x] 4.3 验证 page/buffer cache read miss 只在 terminal success 后发布有效数据，pending 或 failure 期间不暴露 partial/stale 数据
- [x] 4.4 验证 dirty writeback、dirty victim eviction、`sync`/`fsync` 和 persistent `/rw` clean-sync 只在 terminal success 后清除 dirty 或 pending-writeback 状态
- [x] 4.5 确保验证专用 producer 与故障注入保持默认关闭，normal init/userland baseline 不依赖验证 producer 才能完成启动

## 5. 默认关闭验证与诊断覆盖

- [x] 5.1 扩展或新增默认关闭 lifecycle smoke，覆盖 success、issue failure、device error、timeout、cancel 或等价失效路径、重复 completion、迟到 completion、identity mismatch 和 slot reuse protection
- [x] 5.2 覆盖 request-layer producer 的确定性 success/error/timeout/late/repeated completion 场景，并检查 terminal reason、rejection reason 和 slot reuse 计数
- [x] 5.3 覆盖真实 ATA-backed 非轮询路径的成功完成、timeout/error 传播和迟到 completion 拒绝；如 emulator 或 IRQ 时序不可用，记录跳过原因和残余风险
- [x] 5.4 覆盖 cache round-trip、dirty writeback failure retention、dirty victim eviction failure 和 persistent `/rw` clean-sync failure，确认 failure 不会被误映射为 durable success
- [x] 5.5 覆盖默认启动回归，确认未启用 lifecycle smoke 时 normal PID-1/init、`/bin/sh`、默认 boot disk 和默认块设备选择不变

## 6. 构建、静态检查与运行验证

- [x] 6.1 运行最窄可用 `xmake` 构建；如缺少 xmake、x86_64-elf-gcc、x86_64-elf-g++ 或相关 binutils，记录缺失工具和残余风险
- [x] 6.2 对修改的 C++ 源/头运行 clang 辅助检查，尽量匹配 freestanding C++17、x86_64 target、项目 include 路径、无异常、无 RTTI；区分历史诊断、当前变更诊断和 freestanding 配置差异
- [x] 6.3 对修改的 C++ 源/头运行 clangd 或等价辅助诊断；如 clangd 无法匹配交叉编译环境，记录配置差距和残余风险
- [x] 6.4 在 QEMU headless 可用时运行 lifecycle/block I/O smoke，并观测串口 pass/fail marker；如 QEMU、ROM/display、磁盘镜像或 serial logging 不可用，记录跳过原因和残余风险
- [x] 6.5 在 Bochs 可用时对 ATA PIO、中断、端口 I/O 或早期启动相关行为做交叉验证；如 Bochs 或 ROM/display 依赖不可用，记录跳过原因和残余风险
- [x] 6.6 运行默认启动或最窄可用 boot 回归，确认 normal userland baseline 仍可到达预期串口 marker；不可运行时记录阻塞项
- [x] 6.7 整理验证记录，分开列出通过的检查、无法运行的检查及原因、历史诊断、当前变更新增诊断、工具链/配置误报和剩余低层运行风险
