## Context

BigOS 当前默认可运行基线仍是 x86_64 Legacy BIOS/MBR/exFAT，通过 ATA 兼容磁盘路径启动并进入默认 userland。现代块存储后端已经在设备框架、块请求层、异步 completion、MSI-X 和 cache/writeback 集成之上具备内核内部选择能力，但这些能力还需要通过真实硬件风格的仿真器设备组合进行可复现验证。

本 change 只定义和实现验证闭环：让默认关闭的验证路径在支持现代存储设备的仿真器配置下选择现代后端，经普通块请求层、page/buffer cache 和 writeback 进行读写往返，并把默认启动路径作为回归项单独确认。验证工具与记录必须沿用现有 xmake、`tools/boot_debug.py`、QEMU headless 串口 marker、`logs/` 日志和结构化 runtime smoke artifact 风格。

## Goals / Non-Goals

**Goals:**

- 增加默认关闭的现代存储仿真器验证用例，覆盖设备发布、块请求完成、cache-mediated read/write、writeback terminal success 和默认启动回归。
- 让验证工具显式配置现代存储设备，同时保留当前 Legacy BIOS/MBR/exFAT 启动镜像和 ATA 默认启动路径。
- 在验证 artifact 中区分工具不可用、仿真器不支持、现代设备未发布、请求层失败、cache/writeback 失败、timeout/错误完成和默认启动回归失败。
- 保持验证路径 x86_64-only，不新增 ISA、不新增用户可见设备 ABI、不改变 boot handoff、链接地址、页表布局、中断/syscall 向量或磁盘布局。

**Non-Goals:**

- 不实现新的现代存储驱动、不扩展 virtio-blk/NVMe 功能集、不增加多队列或新的中断投递机制。
- 不把现代存储后端设为默认启动依赖，也不把根文件系统切换到现代后端。
- 不引入 UEFI 存储/backend parity，不新增 RISC-V、ARM 或其它 ISA 后端。
- 不要求 Bochs 必须支持现代存储设备；Bochs 只在本地设备模型可用时作为交叉验证或记录跳过。

## Decisions

1. 验证入口作为默认关闭 runtime smoke 用例接入，而不是普通启动路径的一部分。
   - 原因：现代后端仍是能力验证对象，默认用户可见基线应继续独立于现代设备存在。
   - 备选：让普通启动自动优先选择现代后端。这样会把验证设备可用性变成启动前置条件，并扩大回归面。

2. 自动化运行优先使用 QEMU headless 串口 marker 与结构化 artifact。
   - 原因：当前项目已有 QEMU headless 和 serial-marker 检查惯例，适合 CI-like 本地验证；日志路径和跳过记录规则也已经成型。
   - 备选：只写手动验证步骤。手动步骤无法稳定表达跳过原因、失败阶段和残余风险。

3. 验证设备配置由 helper/xmake run 参数显式启用，不改变默认 IDE/ATA 磁盘暴露方式。
   - 原因：同一 raw image 可以继续用于 boot；现代设备作为附加验证设备存在，避免修改 MBR/exFAT 启动布局。
   - 备选：生成独立现代存储启动镜像。会混入启动介质切换问题，超出验证边界。

4. 内核验证逻辑通过内部 selector 选择现代块后端，然后走普通块请求层和 cache/writeback，而不是直接调用驱动私有接口。
   - 原因：需要验证集成后的行为，而不是只证明驱动私有 read/write 能工作。
   - 备选：驱动自测直接操作 virtqueue 或设备寄存器。覆盖面不足，不能证明块层与缓存语义。

5. 失败和跳过必须分层记录。
   - 原因：现代存储验证受本地 QEMU 版本、设备模型、MSI-X 支持、cross toolchain 和镜像生成影响；跳过不能被描述为 runtime 成功。
   - 备选：把环境不可用统一记为失败。会降低 artifact 对本地环境差异的解释力。

## Risks / Trade-offs

- [Risk] QEMU 设备参数或 virtio/MSI-X 支持在本地版本中不可用。→ Mitigation：preflight 阶段检测并把用例标记为 blocked/skipped，记录替代检查和残余风险。
- [Risk] 验证写入影响默认启动镜像内容。→ Mitigation：现代设备作为附加验证介质或受控区域使用，默认启动回归单独运行并确认不依赖现代后端。
- [Risk] 只跑驱动层读写会漏掉 cache/writeback 集成问题。→ Mitigation：验证入口必须通过块请求层、page/buffer cache 和 writeback 成功终态判断通过。
- [Risk] 中断完成路径不稳定导致间歇性 timeout。→ Mitigation：artifact 记录请求阶段、completion 阶段、observed marker、serial log 和 timeout，便于区分设备未发布、未完成和迟到完成。
- [Risk] Bochs 缺少等价现代存储设备模型。→ Mitigation：Bochs 不作为 mandatory pass 条件；若不可用，记录跳过原因和 QEMU 覆盖后的剩余硬件行为风险。

## Migration Plan

1. 梳理现有现代块后端 selector、runtime smoke matrix、boot debug helper、QEMU 参数构造和 validation artifact 字段。
2. 增加现代存储仿真器验证用例的配置入口、preflight 检测、串口 marker 期望和 `logs/` 输出路径。
3. 实现内核默认关闭验证入口：选择现代后端，提交块请求层读写，经 cache/writeback 完成 round trip，并输出 pass/fail marker。
4. 扩展 validation artifact，记录现代设备配置、后端选择结果、请求/cache/writeback 阶段结果、默认启动回归结果、跳过项和残余风险。
5. 保持默认启动路径配置不变，并运行默认启动回归确认现代后端不可用时仍可到达当前 userland baseline。
6. 回滚策略：移除新增验证用例、helper 参数和 smoke 开关即可；不涉及 ABI、磁盘布局、默认启动设备或用户态接口迁移。

## Open Questions

- 暂无。验证对象限定为现有 x86_64 现代存储后端，自动化首选 QEMU headless；其它仿真器覆盖按本地能力记录。
