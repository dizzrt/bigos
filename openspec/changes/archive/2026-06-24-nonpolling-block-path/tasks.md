## 1. 现状梳理与接口边界

- [x] 1.1 梳理 `block_io::submit_sync`、`Request`、`CompletionToken`、per-device queue、scheduler wait queue 和现有 completion API，确认哪些状态可复用、哪些状态需要调整
- [x] 1.2 梳理当前 `BlockDevice` 读写接口、RAM block 后端、ATA PIO 后端、page/buffer cache load/writeback 和 persistent `/rw` clean-sync 的调用链
- [x] 1.3 明确本变更不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、文件系统格式、用户态 ABI 或默认启动设备选择
- [x] 1.4 记录 QEMU/Bochs、xmake、x86_64-elf-gcc/x86_64-elf-g++、ROM/display 和磁盘镜像路径的本地可用性，作为后续验证前置条件

## 2. 块设备 issue 与请求层重构

- [x] 2.1 扩展块设备接口，加入有界 issue/completion 能力或等价 capability 标记，保持公共头最小且不引入 hosted runtime、异常、RTTI 或动态分配依赖
- [x] 2.2 重构 `submit_sync`，使其按“校验请求 -> 占用队列槽 -> arm pending -> issue device work -> scheduler wait -> 返回最终状态”执行
- [x] 2.3 确保 issue 失败、queue full、invalid request、device not ready、would-block、timeout 和 completion rejection 都释放或保留队列槽到正确状态，并返回可诊断状态
- [x] 2.4 保持 RAM block 或同步兼容验证后端能通过同一 request-layer completion/status 路径立即完成，不绕过请求层状态机
- [x] 2.5 审查 request、queue slot、device identity 和 generation 匹配，确保 timeout/cancel 后迟到 completion 不污染复用槽位

## 3. ATA PIO 非轮询迁移

- [x] 3.1 将 ATA PIO 的命令发起、设备选择、数据阶段、write flush 和错误状态整理为有界驱动状态，避免上层请求依赖 whole-request 同步轮询完成
- [x] 3.2 接入 ATA IRQ 或等价 bounded completion source，使 ATA-backed 请求通过 request-layer completion entry 发布最终状态
- [x] 3.3 保持 LBA48、512-byte sector、端口 I/O 顺序、range/size 校验、read/write/flush 错误映射和 persistent test disk 端口约定不变
- [x] 3.4 审查 IRQ handler 中的工作量，确认只做有界设备状态观察、必要 PIO 传输、completion handoff 和 wakeup，不执行 cache policy、filesystem、用户内存访问、阻塞等待、动态分配或 EOI
- [x] 3.5 审查 PIC/LAPIC EOI 所有权，确认块 completion entry 不发送 EOI，不改变 exception/syscall dispatch 或现有中断向量语义

## 4. Cache、writeback 与文件系统语义保持

- [x] 4.1 验证 page/buffer cache read miss 仍通过块请求层装入完整数据，pending 期间不暴露 partial/stale 数据
- [x] 4.2 验证 dirty writeback、dirty victim eviction、`sync`/`fsync` 只在 request-layer terminal success 后清除 dirty 状态
- [x] 4.3 确保 timeout、device error、completion rejection 或 issue failure 会保留 dirty 或 pending-writeback 状态，并向调用者返回确定性错误
- [x] 4.4 验证 persistent `/rw` clean-sync 的 data/metadata commit 行为不变，失败时不发布 durable success
- [x] 4.5 审查不可阻塞上下文边界，确保 cache load/writeback 仍只在允许阻塞的普通上下文执行，IRQ/completion 不触发 cache policy

## 5. 诊断与默认关闭验证

- [x] 5.1 扩展或新增默认关闭 block I/O smoke，覆盖正常请求进入 pending、设备/producer completion、同步 wrapper 唤醒并观察最终状态
- [x] 5.2 覆盖 request timeout、device error、重复 completion 拒绝、迟到 completion 拒绝、issue failure 和 queue slot 复用保护
- [x] 5.3 覆盖 cache round-trip、dirty writeback failure retention、dirty victim eviction failure 和 persistent `/rw` clean-sync 回归
- [x] 5.4 确认诊断、注释和文档只声明有界内核内部 nonpolling block completion，不声明完整 async I/O、用户态 async syscall、DMA、多队列调度、现代存储驱动、后台 writeback 或网络能力
- [x] 5.5 若实现需要更新 `docs/en` 或 `docs/zh`，同步更新对应语言镜像，并保持 repository-relative 路径和当前文档边界

## 6. 构建、静态检查与运行验证

- [x] 6.1 运行最窄可用的 `xmake` 构建；如缺少 xmake、x86_64-elf-gcc、x86_64-elf-g++ 或相关 binutils，记录缺失工具和残余风险
- [x] 6.2 对修改的 C++ 源/头运行 clang 辅助检查，尽量使用 freestanding C++17、x86_64 target、项目 include 路径、无异常、无 RTTI；区分历史诊断、当前变更诊断和 freestanding 配置差异
- [x] 6.3 对修改的 C++ 源/头运行 clangd 或等价辅助诊断；如 clangd 无法匹配交叉编译环境，记录配置差距和残余风险
- [x] 6.4 在 QEMU headless 可用时运行迁移后的默认关闭 block I/O smoke，并观测串口 pass/fail marker；如 QEMU、ROM/display、磁盘镜像或 serial logging 不可用，记录跳过原因和残余风险
- [x] 6.5 在 Bochs 可用时对 ATA PIO、中断、端口 I/O 或早期启动相关行为做交叉验证；如 Bochs 或 ROM/display 依赖不可用，记录跳过原因和残余风险
- [x] 6.6 运行默认启动或最窄可用 boot 回归，确认 normal PID-1/userland baseline 仍可到达预期串口 marker；不可运行时记录阻塞项
- [x] 6.7 整理验证记录，分开列出通过的检查、无法运行的检查及原因、历史诊断、当前变更新增诊断、工具链/配置误报和剩余低层运行风险
