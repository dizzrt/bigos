## 1. 信号核心子系统

- [x] 1.1 新增 `include/bigos/signal.h` 与 `kernel/core/signal/`：定义固定信号编号集合（非实时、≤64）、`SigDisposition`（默认/忽略/handler 入口地址）与每信号默认动作表（Terminate/Ignore），约定 `SIGKILL` 不可捕获/不可阻塞。
- [x] 1.2 实现每进程信号状态操作纯逻辑：pending 位图置位/清除/取最低位、阻塞掩码应用（`pending & ~mask`）、处置查询；全部 O(1)、无分配、无阻塞。
- [x] 1.3 实现 `signal::kill(target, signo)` 投递：合法时置位目标 pending（`SIGKILL` 等不可阻塞信号忽略目标 mask），非法信号号返回 `-EINVAL`；不在此函数内做权限判定（由 syscall 层接线 `cred::may_signal`）。
- [x] 1.4 接口与上下文安全审查：投递/查询路径无 `kmalloc`/`alloc_kernel_pages`/`free`、不阻塞、不 bulk 输出；信号集合宽度与位图类型一致。

## 2. IRQ-return 信号投递与信号帧

- [x] 2.1 实现 `signal::deliver_pending_to_user(InterruptFrame*, Process*)`：选最低位未阻塞 pending 信号，按处置分派——默认 Terminate（或 `SIGKILL`）走 `fault_current_and_exit`/exit 并把信号号编码进退出/fault 状态；Ignore 清位丢弃；handler 走信号帧构造。
- [x] 2.2 实现用户 handler 信号帧构造：用 VMA-backed 用户范围校验确认用户栈可写、对齐、不越界，在用户栈保存被中断用户上下文与信号号，改写 `frame->rip=handler`、第一参数寄存器=signo、`frame->rsp=新帧`，并在 handler 期间把该信号加入阻塞掩码、清除其 pending 位；校验失败确定性终止进程（不写非法用户地址、不分配、不阻塞）。
- [x] 2.3 实现 `signal::sigreturn(InterruptFrame*)`：从用户栈信号帧恢复用户可见寄存器与受约束的 rip/rsp/rflags 及旧掩码；强制段寄存器为用户段、rflags 保持 IF 且不提升特权敏感位、rip/rsp 限定用户低半区；不信任用户栈上的特权字段。
- [x] 2.4 在 [interrupt.cc](kernel/core/irq/interrupt.cc) 外部 IRQ 分支、`maybe_preempt_on_irq_return` 之后、iretq 之前接入投递点：仅当 `(frame->cs & 0x3) == 0x3`、当前进程存在且有未阻塞 pending 信号时调用 `deliver_pending_to_user`；内核态被中断帧 MUST NOT 投递。
- [x] 2.5 中断/ABI 与提权安全审查：审查 `interrupt.s`/`interrupt.cc`/`switch.s` 的 `InterruptFrame` 布局与返回顺序，确认投递点不破坏 iretq 约定、不与 timer 抢占钩子顺序冲突、不发 i8259 EOI；sigreturn 不可被伪造帧用于返回内核特权上下文。

## 3. 信号相关 syscall

- [x] 3.1 在 [syscall.h](include/bigos/syscall.h) 的 `SyscallNumber` 末尾追加 `SYS_KILL = 16`、`SYS_SIGACTION = 17`、`SYS_SIGPROCMASK = 18`、`SYS_SIGRETURN = 19`，不改动既有号位与寄存器 ABI 注释。
- [x] 3.2 在 [syscall.cc](kernel/core/syscall/syscall.cc) 的 `dispatch` 增加分支：`SYS_KILL` 查找目标、调用 `cred::may_signal` 强制权限、按结果返回 0/`-ESRCH`/`-EPERM`/`-EINVAL`；`SYS_SIGACTION`/`SYS_SIGPROCMASK` 校验信号号与不可捕获/不可阻塞约束后更新处置/掩码并回写旧值；`SYS_SIGRETURN` 调用 `signal::sigreturn`。全部不发 EOI。
- [x] 3.3 在 [errno.h](include/bigos/errno.h) 补齐缺失的错误码（如 `ESRCH`），保持单一来源、不重复定义。
- [x] 3.4 中断/ABI 审查：确认新增分支不发送 i8259 EOI、不放宽异常/IRQ 门、不改变 rax 返回约定与向量/DPL 布局；`SYS_SIGRETURN` 改写返回上下文的路径不破坏 `InterruptFrame` 约定。

## 4. 进程生命周期与 fork/exec 接线

- [x] 4.1 在 [proc.h](include/bigos/proc.h) 的 `Process` 追加信号字段（pending 位图、阻塞掩码、每信号处置表），保持为追加字段、不重排既有布局。
- [x] 4.2 在进程创建路径初始化信号状态：init 与非 fork ELF 创建置全默认处置、空掩码、空 pending；确认不引入新的分配失败路径。
- [x] 4.3 在 `fork_current` 让子进程逐字段继承父进程处置表与阻塞掩码、清空子进程 pending；确认不改变 COW/引用计数/回滚与父子返回值语义。
- [x] 4.4 在 `exec` 路径把用户 handler 处置一律重置为默认（旧 handler 地址失效），保留掩码与 pending（按 design 决策 7 固化语义）。
- [x] 4.5 在子进程退出/被信号终止进入 zombie 的路径向父进程 pending 置位 `SIGCHLD`（默认 Ignore），不改变现有 `wait` 唤醒与 reaper 回收行为。
- [x] 4.6 内存/生命周期审查：信号字段初始化覆盖所有创建路径、fork 继承与 pending 清空正确、默认 Terminate 复用既有 reaper、`SIGCHLD` 置位无新增分配。

## 5. 权限强制点接线

- [x] 5.1 在 `SYS_KILL` 分支接入 [cred.cc](kernel/core/proc/cred.cc) 的 `may_signal(actor, target)` 作为唯一强制点；确认 `may_signal` 判定逻辑（root 放行、身份匹配、非法输入拒绝）零改动。
- [x] 5.2 审查强制点：拒绝时不修改目标 pending、返回确定性 `-EPERM`；目标不存在返回 `-ESRCH`，先于权限判定或按文档化顺序确定性处理。

## 6. 验证开关与 smoke

- [x] 6.1 在 `xmake.lua` 新增默认关闭开关 `signal_smoke`（定义 `BIGOS_SIGNAL_SMOKE`），保留现有 smoke 矩阵不删除。
- [x] 6.2 实现默认关闭的 smoke 路径：发射 `BIGOS_SIGNAL_PASSED`/`BIGOS_SIGNAL_FAILED`，覆盖「kill 默认动作终止目标」「用户 handler 捕获 + sigreturn 恢复」「掩码阻塞 pending、解除后投递」「`SIGKILL` 不可捕获/不可阻塞」「越权 kill 被 `may_signal` 拒绝」；smoke 用户程序的 handler 末尾自行 `int 0x80` 触发 `SYS_SIGRETURN`（本阶段无 libc trampoline）。

## 7. C++ 辅助静态检查

- [x] 7.1 对新增/修改的 C++ 源与头运行 clang 与 clangd 辅助静态检查，尽量贴近 GCC 交叉构建环境（freestanding C++17、x86_64 目标、项目 include 路径、无 hosted 运行时/异常/RTTI）；若等价 flag 不可用则记录差距与残留风险。
- [x] 7.2 修复本次变更引入的 clang/clangd 错误，确认或修复有效新增告警；验证记录区分历史诊断、本次变更诊断与工具链/freestanding 误报。

## 8. 构建与运行时验证

- [x] 8.1 运行最窄可用构建（`xmake`）确认信号模块、`Process` 字段、syscall 分支与 IRQ-return 投递点编译通过；clang/clangd 仅作辅助信号，不替代 x86_64-elf-gcc 构建。
- [x] 8.2 在可用时运行 QEMU headless serial-marker smoke（`uv run python tools/boot_debug.py run --emulator qemu --display none ...` 并配 `--signal_smoke=y`），断言 `BIGOS_SIGNAL_PASSED`；涉及 ring3/syscall/IRQ-return/用户栈行为时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证。
- [x] 8.3 若 QEMU/Bochs、ROM/显示、交叉工具链或磁盘镜像不可用，显式记录缺失工具、跳过的验证、替代检查与残留风险，不得声称已做运行时验证。

## 9. 源码契约/行为断言测试

- [x] 9.1 用 `uv run pytest` 增补源码契约/行为断言测试（沿用behavior assertion validation baseline 启动的行为断言轨道）：覆盖新增 syscall 号位固定（`SYS_KILL=16`..`SYS_SIGRETURN=19`）、`Process` 信号字段存在、投递路径无分配、`SIGKILL` 不可捕获/不可阻塞、`may_signal` 接线于 kill、IRQ-return 投递点仅对用户态帧，以及 smoke marker 行为断言。
- [x] 9.2 对新增/修改的 Python 文件运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`，修复新引入的 lint/类型/格式/测试问题；若 `uv` 不可用则显式记录该阻塞。

## 10. 文档与验证记录

- [x] 10.1 更新相关文档（`docs/en` 为 canonical，`docs/zh` 同步匹配相对路径）：记录信号编号集合与默认动作、pending/mask/处置表语义、IRQ-return 投递与信号帧/sigreturn 机制、新增 syscall 号、`SIGCHLD` 与 fork/exec 信号语义、`signal_smoke` 开关；不暗示更改 boot/向量/DPL/页表/CR3/ABI。
- [x] 10.2 整理验证记录：分别列出已通过检查、因依赖缺失无法运行的检查与原因及残留风险、历史诊断、本次变更引入的问题；明确非目标（实时信号、SIGSTOP/CONT、sigaltstack、EINTR 重启、libc trampoline、SMP 跨核投递）与已知限制（单一 IRQ-return 投递点导致自投信号延迟）。
