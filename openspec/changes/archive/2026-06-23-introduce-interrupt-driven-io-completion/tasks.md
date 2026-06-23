## 1. 状态模型与边界梳理

- [x] 1.1 梳理现有 `block_io::Request`、per-device queue、`submit_sync`、`BlockDevice` 同步接口和 scheduler wait queue，确认 completion 状态需要复用和新增的字段
- [x] 1.2 定义请求生命周期状态，覆盖 queued、pending、completed-success、completed-error、timeout-or-cancelled、invalid 和重复完成拒绝
- [x] 1.3 审查请求和 completion 状态的所有权，确保 pending 请求在完成、timeout 或取消前保持有效，不依赖 IRQ 中动态分配或 hosted runtime
- [x] 1.4 明确本变更不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI、默认 boot disk 或 persistent `/rw` 初始化顺序

## 2. 请求层 completion 实现

- [x] 2.1 扩展 `include/bigos/block_io.h` 中的请求状态、状态码和必要 API，保持公共头最小且兼容现有同步消费者
- [x] 2.2 在 `kernel/core/block_io.cc` 中实现 pending arm、一次性完成、重复完成拒绝、timeout-or-cancelled 和 stale completion 防护
- [x] 2.3 新增 IRQ-safe completion entry，只允许更新 bounded completion 状态并唤醒等待者，禁止提交请求、阻塞、分配、释放、cache/writeback、文件系统访问和 irqchip EOI
- [x] 2.4 保留现有同步 dispatch 行为，使当前 RAM block、ATA-backed 设备和 cache 消费路径在未选择 pending path 时继续返回最终状态

## 3. Scheduler 等待与 IRQ 边界集成

- [x] 3.1 将 pending 请求等待接入现有 scheduler wait queue，普通可阻塞内核线程可等待完成或有限 timeout
- [x] 3.2 确保 completion wakeup 对等待线程 exactly-once 生效，并正确处理 completion 与 timeout/cancel 的竞争
- [x] 3.3 审查不可阻塞上下文边界，确保 IRQ、timer、scheduler critical section、preemption-disabled、CPU exception 或等价路径不能提交请求或等待完成
- [x] 3.4 审查 EOI 所有权，确认 completion entry 不发送 PIC/LAPIC EOI，不改变 syscall/exception dispatch 或外部 IRQ ownership

## 4. 验证路径与诊断

- [x] 4.1 扩展默认关闭 block I/O 或新增相邻 smoke，覆盖 pending 请求、IRQ-like producer 完成、等待线程唤醒和最终状态观测
- [x] 4.2 覆盖重复完成拒绝、timeout-before-completion、stale completion 不污染复用队列槽、设备错误状态传播和非法请求状态传播
- [x] 4.3 增加或更新源级测试，检查 completion API、状态名称、forbidden-context 约束、IRQ-safe 完成入口和文档边界
- [x] 4.4 确认诊断和文档只声明有界内核内部 interrupt-driven completion，不声明完整 async I/O、用户态 async syscall、DMA、多队列调度、现代存储驱动或后台 writeback

## 5. 日志输出目录迁移

- [x] 5.1 将 `tools/boot_debug.py` 默认 `LOG_DIR` 从 `log` 改为 `logs`，同步默认 serial log、QEMU/QEMU-GDB/UEFI serial log、Bochs diagnostic log、runtime smoke artifact 和 per-case serial log 目录
- [x] 5.2 将 `xmake/run_targets.lua` 中 helper-managed 默认串口日志路径从 `log/*.serial.log` 改为 `logs/*.serial.log`
- [x] 5.3 保持显式传入的 `--serial-log`、`--output`、`--serial-log-dir` 等自定义路径原样使用，不做 `log/` 到 `logs/` 的自动改写
- [x] 5.4 更新 `tests/test_boot_debug.py` 和相关源级测试断言，覆盖默认路径迁移到 `logs/`，并确认自定义日志路径仍不被改写
- [x] 5.5 同步更新 `docs/en` 与 `docs/zh` 中当前 helper、runtime smoke、UEFI、memory validation 和 boot layout 示例里的默认日志路径引用；历史失败记录如保留 `log/`，需明确其为历史记录而非当前默认
- [x] 5.6 搜索仓库中剩余 `log/` 默认路径引用，区分需要迁移的当前默认行为、用户自定义示例和历史验证记录，避免遗漏当前默认输出路径

## 6. 构建、静态检查与运行验证

- [x] 6.1 运行最窄可用的 `xmake` 构建；如缺少 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake 或相关 binutils，记录缺失工具和残余风险
- [x] 6.2 对修改的 C++ 源/头运行 clang 辅助检查，尽量使用 freestanding C++17、x86_64 target、项目 include 路径、无异常、无 RTTI；区分历史诊断、当前变更诊断和 freestanding 配置差异
- [x] 6.3 对修改的 C++ 源/头运行 clangd 或等价辅助诊断；如 clangd 无法匹配交叉编译环境，记录配置差距和残余风险
- [x] 6.4 对修改的 Python helper 运行 `uv run ruff check tools/boot_debug.py`、`uv run ruff format --check tools/boot_debug.py`、`uv run pyright tools/boot_debug.py` 和相关 `uv run pytest ...`；如果 `uv` 不可用则明确记录 blocker
- [x] 6.5 运行相关源级测试；Python 测试或 helper 必须使用 `uv run ...`，如果 `uv` 不可用则明确记录 blocker
- [x] 6.6 在 QEMU headless 可用时运行 completion 相关默认关闭 smoke 并观测串口 marker，确认默认生成路径使用 `logs/`；如 QEMU、Bochs、ROM/display、磁盘镜像或 emulator 配置不可用，记录跳过原因和残余风险
- [x] 6.7 整理验证记录，分开列出通过的检查、无法运行的检查及原因、历史诊断、当前变更新增诊断和剩余低层运行风险
