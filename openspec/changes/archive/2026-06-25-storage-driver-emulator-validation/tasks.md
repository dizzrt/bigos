## 1. 现状梳理与验证边界

- [x] 1.1 梳理现代块存储后端发布、内部 selector、块请求层、page/buffer cache、writeback 与 runtime smoke matrix 的现有调用链
- [x] 1.2 梳理 QEMU/Bochs 启动 helper、xmake run target、串口日志、validation artifact 与 `logs/` 路径约束
- [x] 1.3 确认本 change 不改变 boot handoff、链接地址、页表布局、中断/syscall 向量、磁盘启动布局、默认 ATA/exFAT 启动路径、用户态 ABI 或 ISA 范围
- [x] 1.4 记录本地 `xmake`、`uv`、`x86_64-elf-*`、QEMU、Bochs、ROM/display、serial capture 与现代存储设备模型支持情况，作为验证前置条件

## 2. Runtime smoke 与仿真器配置

- [x] 2.1 新增默认关闭的现代存储仿真器验证用例，列出所需构建开关、首选 QEMU headless 路径、期望 marker、超时、日志路径与 artifact 输出
- [x] 2.2 扩展 helper/xmake 参数构造，使验证用例显式附加现代存储设备，同时保留现有 generated boot image 与默认 ATA-compatible 启动磁盘语义
- [x] 2.3 增加 preflight 检测，区分工具链缺失、仿真器不可用、设备模型不支持、serial capture 不可用、磁盘镜像生成失败和中断投递不可用
- [x] 2.4 确认验证用例默认关闭，不影响既有 memory、timer、scheduler、syscall、filesystem、blocking、userland 与默认 init smoke 默认值

## 3. 内核默认关闭验证入口

- [x] 3.1 增加或接入默认关闭的内核验证入口，通过内核内部 selector 选择现代块存储后端并记录发布/不可用状态
- [x] 3.2 通过普通块请求层提交现代后端读写请求，确保 success、timeout、device error、issue failure、late/rejected completion 在请求层边界可区分
- [x] 3.3 通过 page/buffer cache 与 writeback 路径执行至少一次写后读 round trip，成功判据依赖请求层 terminal success
- [x] 3.4 输出明确的 pass/fail marker，并在失败时记录后端发布、请求完成、cache/writeback 或 timeout/error 的具体阶段
- [x] 3.5 审查验证入口只在允许阻塞的普通内核上下文运行，completion/IRQ 路径不执行 cache policy、文件系统策略、阻塞等待或动态分配

## 4. Validation artifact 与默认启动回归

- [x] 4.1 扩展 runtime smoke artifact 字段，记录 emulator backend、display mode、现代存储设备配置、expected/observed marker、serial log、阶段结果与残余风险
- [x] 4.2 将环境不可用记录为 skipped/blocked，而不是 passed；同时记录已执行的替代检查和剩余风险
- [x] 4.3 增加默认启动回归记录，确认现代存储验证关闭时 normal boot 仍可经现有启动/存储基线到达默认 userland
- [x] 4.4 确认现代存储验证通过与默认启动回归结果分开记录，任何一项 skipped/failed 都不被另一项掩盖

## 5. 文档与边界说明

- [x] 5.1 更新必要的验证文档或开发说明，说明现代存储验证是默认关闭的仿真器验证路径，不是默认启动依赖
- [x] 5.2 如修改 `docs/en` 或 `docs/zh`，同步更新对应语言镜像并保持相对路径结构一致
- [x] 5.3 审查文档与诊断文字，不声明新 ISA、完整存储矩阵、UEFI backend parity、用户可见设备 ABI、默认 FS 后端替换或 release-grade CI

## 6. 构建、静态检查与运行验证

- [x] 6.1 运行最窄可用的 `xmake` / GCC 交叉构建，覆盖默认配置与启用现代存储验证配置；工具不可用时记录缺失项和残余风险
- [x] 6.2 对修改的 C++ 源/头运行 clang 辅助检查，尽量匹配 freestanding C++17、x86_64 target、项目 include、无 hosted runtime、无 exceptions、无 RTTI；区分历史诊断、当前变更诊断和 freestanding 误报
- [x] 6.3 对修改的 C++ 源/头运行 clangd 或等价辅助诊断；如 compile database 或交叉配置不可用，记录配置差距和残余风险
- [x] 6.4 如修改 Python helper 或测试，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 与相关 `uv run pytest`；`uv` 不可用时记录 blocker
- [x] 6.5 在 QEMU headless 可用且现代存储设备模型满足要求时运行现代存储验证用例，确认发布、请求完成、cache/writeback round trip 与 pass/fail marker，并将日志写入 `logs/`
- [x] 6.6 运行默认启动回归，确认验证路径关闭时现有 userland baseline 不依赖现代存储后端；不可运行时记录阻塞项
- [x] 6.7 在 Bochs 或其它交叉验证路径支持相关设备模型时记录交叉验证结果；不可用时记录跳过原因和剩余硬件行为风险
- [x] 6.8 运行 OpenSpec 校验与 targeted search，确认新建 change artifact 不包含 roadmap 编号引用，并记录 validation notes

## 7. 低层风险审查

- [x] 7.1 审查 boot/image 配置，确认未改变 boot handoff、链接地址、页表布局、磁盘启动布局和默认启动设备选择
- [x] 7.2 审查 IRQ/completion 边界，确认 EOI 所有权、allocation-free、nonblocking、late completion 拒绝和请求槽复用语义未被破坏
- [x] 7.3 审查 MMIO/设备配置和 DMA buffer 生命周期，确认硬件访问顺序、对齐、物理地址使用和失败路径可诊断
- [x] 7.4 整理验证记录，分开列出通过检查、无法运行检查及原因、历史诊断、当前变更新增诊断、工具链/配置误报和剩余风险
