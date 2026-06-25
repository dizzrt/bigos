## Why

BigOS 当前已经具备基于 PIT tick 的内核线程阻塞睡眠能力，但用户态只能读取 tick/秒级时间，缺少一个可让普通静态用户程序主动让出 CPU、等待一段时间后继续执行的阻塞式 sleep syscall。

补齐这个接口可以让 `/bin/sh`、用户态 smoke、简单脚本式程序和后续终端/交互能力避免 busy loop，同时复用现有 scheduler timeout sleep 边界，不引入完整 POSIX timer 或高精度时钟模型。

## What Changes

- 新增有界阻塞式 sleep syscall，采用追加 syscall 编号，不改变既有 `int 0x80` ABI、参数寄存器、返回值约定或 syscall no-EOI 语义。
- 接口以毫秒为用户可见单位：`SYS_SLEEP_MS(milliseconds)`；内核将毫秒向上取整为 PIT tick，并通过现有 `timer::sleep_for()` / `sched::sleep_for()` 阻塞当前普通线程。
- syscall 返回 `0` 表示睡眠到期或零时长立即成功；非法参数、tick 转换溢出、超出有界最大时长、或当前上下文不可阻塞时返回确定性负 errno。
- 用户态 libc 暴露 BigOS 明确语义包装 `bigos_sleep_ms(unsigned long milliseconds)`，并可提供受限 `sleep(unsigned int seconds)` 包装；`sleep()` 首期不承诺 POSIX signal interruption 后返回剩余秒数。
- 增加默认关闭的用户态 sleep runtime smoke，验证 sleep syscall 会阻塞到至少预期 tick 下界且不会 busy-wait。
- 更新 syscall/libc/time 文档边界，明确本变更不提供完整 POSIX `nanosleep`、timerfd、alarm、interval timer、高精度 clock、异步 timer、或 signal-interruptible sleep 语义。

## Capabilities

### New Capabilities

- `blocking-sleep-syscall`: 定义用户可见阻塞式 sleep syscall 的 ABI、参数单位、返回/errno 语义、内核 scheduler/timer 复用方式、上下文限制和验证要求。

### Modified Capabilities

- `bounded-syscall-surface`: 将阻塞式 sleep 追加到 BigOS 有界 syscall surface，并要求保持既有 syscall 编号、寄存器 ABI、`VECTOR_SYSCALL = 0x80` 和 no-EOI 语义不变。
- `portable-libc-subset`: 将 `bigos_sleep_ms()` 和受限 `sleep()` 纳入 bounded POSIX-like wrapper subset，并明确不扩大为完整 POSIX time/sleep API。
- `runtime-smoke-validation`: 在 runtime smoke matrix 中增加或覆盖阻塞式 sleep 用户态验证，要求记录 marker、日志、跳过条件和剩余风险。

## Impact

- 受影响子系统：syscall dispatch、timer/scheduler blocking primitives、user libc、user smoke/build packaging、runtime smoke matrix、相关 OpenSpec/文档。
- 预计代码路径：
  - `include/bigos/syscall.h`：追加 syscall number 和 ABI 注释。
  - `kernel/core/syscall/syscall.cc`：新增 `sys_sleep_ms` 分发和错误映射。
  - `include/bigos/timer.h` / `kernel/core/timer/timer.cc`：必要时补充毫秒到 tick 的有界转换 helper，或在 syscall 层局部实现。
  - `user/libc/include/sys_nr.h`、`user/libc/include/unistd.h`、`user/libc/include/time.h`、`user/libc/syscall.c`：新增用户态编号、声明和包装。
  - `user/smoke/**`、`xmake.lua`、`tools/bigosdev/**`：新增默认关闭 sleep smoke 和 runtime marker 配置。
- 架构假设：当前实现面向 x86_64 `int 0x80` syscall 路径和现有 PIT `TIMER_HZ` tick；不改变 IDT/syscall vector、GDT/TSS、CR3 切换、页表布局、boot handoff ABI 或磁盘布局。
- 内存与调度假设：sleep syscall 只在可阻塞的普通用户进程 syscall 上下文执行；不从 IRQ、异常、timer tick、fatal diagnostic、scheduler critical section 或不可阻塞上下文进入；阻塞状态继续由现有 scheduler sleep list 管理且不在 IRQ 路径分配内存。
- 工具链与验证假设：实现验证使用 `x86_64-elf-gcc`/xmake 构建；runtime smoke 优先通过 `uv run python -m tools.bigosdev ...` 的 QEMU headless serial marker 检查，Bochs 可作为低层 timer/syscall 行为交叉验证；缺少工具链、QEMU/Bochs、ROM/display 或 serial oracle 时必须记录跳过和剩余风险。
- 非目标：不实现完整 POSIX `nanosleep(2)`/`clock_nanosleep(2)`、高精度计时器、进程级 alarm、timerfd、异步 signal 唤醒、可取消 sleep、SMP 定时器迁移语义、或完整 hosted libc time API。
