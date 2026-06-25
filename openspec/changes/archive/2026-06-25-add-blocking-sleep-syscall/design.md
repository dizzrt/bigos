## Context

当前 BigOS 已有三层相关基础：

- PIT IRQ0 以 `TIMER_HZ = 100` 推进单调 tick。
- `sched::sleep_for(ticks)` / `timer::sleep_for(ticks)` 可以把当前普通内核线程挂入 scheduler sleep list，等待 tick deadline 到期后重新 runnable。
- 用户态已有 `SYS_GET_TICK`、`SYS_GET_TIME`、`get_tick()` 和 `time()`，但没有用户可见的阻塞式 sleep。

因此本变更不是新增定时器子系统，而是把现有 scheduler timeout sleep 通过有界 syscall ABI 暴露给普通用户进程。影响边界穿过 `kernel/core/syscall`、`kernel/core/timer`/`kernel/core/sched`、`user/libc`、用户态 smoke 和 runtime smoke tooling；不修改 boot 地址、linker 地址、IDT 向量、syscall vector、CR3 切换、页表布局或磁盘布局。

用户可见目标是“让出 CPU 并在粗粒度时间后继续运行”，不是完整 POSIX timer/time API。当前信号系统已有 pending/delivery 能力，但 scheduler sleep primitive 不是 signal-interruptible wait；首期 sleep syscall 因此不承诺被 signal 打断后返回剩余时间。

## Goals / Non-Goals

**Goals:**

- 追加一个用户态阻塞式 sleep syscall，保持既有 `int 0x80` register ABI 和 no-EOI 规则。
- 使用毫秒作为用户可见单位，由 syscall 层或 timer helper 向上取整转换为 tick。
- 复用现有 scheduler sleep list，不在 IRQ/timer tick 路径分配内存。
- 为 libc 暴露 `bigos_sleep_ms()`，并提供受限 `sleep(seconds)` 包装。
- 补充默认关闭 runtime smoke，验证用户态 sleep 阻塞行为可观察。
- 明确参数、溢出、不可阻塞上下文和超出有界时长的 errno 映射。

**Non-Goals:**

- 不实现完整 POSIX `nanosleep`、`clock_nanosleep`、`usleep`、`alarm`、interval timer、timerfd 或异步 timer。
- 不实现 signal-interruptible sleep、sleep cancellation、内核剩余时间写回、或 signal handler 立即唤醒 sleeping 线程。
- 不引入高精度时间源、HPET/APIC timer 抽象、SMP timer migration、per-process timer queue 或实时调度语义。
- 不改变 `VECTOR_SYSCALL = 0x80`、syscall 编号既有值、GDT/TSS、用户态 entry/return、page fault recovery、boot image 或存储布局。

## Decisions

### Decision 1: syscall 使用毫秒单位，命名为 `SYS_SLEEP_MS`

`SYS_SLEEP_MS(milliseconds)` 接收一个无符号 64-bit 毫秒数，返回 `0` 或负 errno。毫秒单位比秒级 `sleep` 更适合作为底层 ABI：用户态 `sleep(seconds)` 可以无损降级为毫秒，而后续简单交互程序也能使用小于一秒的粗粒度等待。

备选方案：

- 只提供 `SYS_SLEEP(seconds)`：实现更小，但 ABI 粒度过粗，后续 shell/终端交互测试容易退化为长等待。
- 提供 `SYS_NANOSLEEP(timespec*)`：更接近 POSIX，但需要用户指针结构、秒/纳秒规范化、剩余时间写回和 signal interruption 语义，超出当前 bounded libc/time 能力边界。
- 直接暴露 tick sleep：实现最直接，但把 PIT `TIMER_HZ` 细节泄漏给用户 ABI，后续换 timer source 时用户程序语义不稳定。

### Decision 2: 毫秒到 tick 采用向上取整和有界上限

转换规则为：

```
ticks = ceil(milliseconds * TIMER_HZ / 1000)
```

`milliseconds == 0` 直接返回成功，不进入 scheduler。非零毫秒必须转换成至少一个 tick。转换前检查乘法和加法溢出，并设置一个明确的最大 sleep 时长常量 `SYS_SLEEP_MS_MAX`。该上限选择接近 tick deadline 安全上限，而不是人为压低到 24 小时：实现按当前 `timer::ticks()` 快照和 `UINT64_MAX` 可用余量推导可接受 tick 数，再反推最大毫秒请求，确保 `timer::ticks() + ticks` 不会回绕。

备选方案：

- 截断转换：小于一个 tick 的请求会变成零等待，用户可见语义不符合“阻塞式 sleep”预期。
- 保守固定上限（例如 24 小时）：更容易直观审查，但无必要地限制 syscall ABI；本变更选择接近 tick 安全上限，同时保持溢出检查。
- 不设上限：代码更短，但 deadline 回绕会把长 sleep 变成短 sleep 或立即 timeout，属于低层时间语义漏洞。

### Decision 3: syscall 层映射 scheduler 返回值为 POSIX-style errno

内核实现建议新增 `__detail::sys_sleep_ms(uint64_t milliseconds)`：

- `0` 毫秒返回 `0`。
- 非法或超界参数返回 `-EINVAL`。
- `sched::can_block()` 为 false 或 `timer::sleep_for()` 返回 `WAIT_BLOCK_FORBIDDEN` 时返回 `-EWOULDBLOCK`。
- 正常到期时 `timer::sleep_for()` 返回 `WAIT_TIMEOUT`，syscall 对用户返回 `0`。
- 其他未预期 scheduler 返回值按保守路径返回 `-EIO` 或 `-EWOULDBLOCK`，并在实现中保持可审查的注释。

这个映射把 scheduler 内部 `WAIT_TIMEOUT = -110` 等非 errno 值隔离在内核内部，不泄漏到用户 ABI。

### Decision 4: sleep 不做 signal-interruptible wait

首期 sleep syscall 在 sleep list deadline 到期后返回。信号 pending 可以在后续用户返回边界按现有机制投递，但不会中途把 sleeping 线程唤醒；`sleep(seconds)` 的剩余秒数只由 libc 通过 tick 前后差值估算，不由内核写回或 signal interruption 驱动。

理由：

- 当前 `sched::sleep_for()` 没有 wait queue predicate 或 signal wakeup 订阅点。
- 给 `kill()` 增加对任意 sleeping thread 的跨路径唤醒会扩大 signal/process/scheduler 交互面，需要单独规格化。
- BigOS 现有 libc 明确是 bounded POSIX-like subset，本阶段可以暴露 `bigos_sleep_ms()` 作为精确定义接口。

备选方案是实现 interruptible sleep wait queue，但这会把本变更从 syscall 暴露扩展为 scheduler/signal 语义变更，风险和验证成本明显更高。

### Decision 5: libc 分层暴露 BigOS 明确接口和受限兼容接口

用户态 libc 增加：

- `int bigos_sleep_ms(unsigned long milliseconds)`：直接映射 `SYS_SLEEP_MS`，成功返回 0，失败返回 -1 并设置 `errno`。
- `unsigned int sleep(unsigned int seconds)`：调用 `bigos_sleep_ms(seconds * 1000)`，成功返回 0；失败时通过 `get_tick()` 前后差值估算已经睡过的整秒数，并返回 `max(seconds - elapsed_seconds, 0)`，从而比固定返回 `seconds` 更细分。若转换溢出或 syscall 立即失败，通常返回原始 `seconds`；若后续引入早醒语义，该 wrapper 也能按 tick 估算剩余秒数。

`time.h` 继续避免宣称完整 timer API；`unistd.h` 可声明 POSIX-like `sleep`，BigOS-specific helper 放在同一 bounded unistd surface 中。

### Decision 6: runtime smoke 以 tick 下界验证阻塞行为

新增默认关闭 smoke，例如 `sleep_syscall_smoke` 或并入用户态 runtime smoke matrix 的 narrow case：

1. 用户程序读取 `get_tick()` 得到 `start`。
2. 调用 `bigos_sleep_ms(30)` 或更稳定的 `bigos_sleep_ms(50)`。
3. 再次读取 `get_tick()` 得到 `end`。
4. 验证 `end - start >= ceil(ms * TIMER_HZ / 1000)`，并允许上界宽松漂移。
5. 成功输出固定 marker，例如 `BIGOS_SLEEP_SYSCALL_PASSED`；失败输出 `BIGOS_SLEEP_SYSCALL_FAILED`。

该验证证明用户进程可通过 syscall 阻塞并恢复，不试图验证精确实时延迟。

## Risks / Trade-offs

- [Risk] tick 粒度只有 10ms，短毫秒 sleep 会被向上取整，实际延迟可能更长。→ Mitigation: 规格明确是 coarse tick-based sleep，smoke 只验证下界，不验证严格实时精度。
- [Risk] deadline tick 加法溢出导致立即唤醒或超长等待。→ Mitigation: 在 syscall 层设 `SYS_SLEEP_MS_MAX` 并检查毫秒到 tick 转换和 `now + ticks` 的边界。
- [Risk] 用户误以为 `sleep()` 是完整 POSIX signal-interruptible sleep。→ Mitigation: 首期主接口命名为 `bigos_sleep_ms()`，`sleep()` 文档和 specs 明确 bounded wrapper；剩余秒数来自 tick 估算，不代表完整 POSIX signal interruption 语义。
- [Risk] syscall 从不可阻塞上下文调用 scheduler sleep。→ Mitigation: 保持 `sched::can_block()` guard，并把 forbidden context 映射为 `-EWOULDBLOCK`。
- [Risk] runtime smoke 时间等待过短导致 QEMU/Bochs 调度漂移下误判。→ Mitigation: 使用 tick 下界和足够大的毫秒值，允许上界漂移，只依赖 monotonic tick。

## Migration Plan

1. 追加 syscall 编号和内核分发，不改动既有编号。
2. 增加 libc 编号、声明和 wrappers。
3. 增加用户态 smoke 与 xmake/default-off 配置。
4. 更新 runtime smoke matrix 和相关文档边界。
5. 运行 source checks、xmake 构建、QEMU headless marker smoke；低层 timer/syscall 风险需要时补 Bochs 交叉验证。

回滚策略：移除新增 syscall case、libc wrappers、smoke case 和文档/spec delta；由于 syscall 编号为追加项，回滚不影响既有 ABI。

## Resolved Decisions

- `SYS_SLEEP_MS_MAX` 选择接近 tick deadline 安全上限，通过转换和 deadline 溢出检查保证不回绕。
- `sleep(seconds)` 在失败或未来早醒路径中按 `get_tick()` 前后差值估算并返回剩余秒数，而不是固定返回原始 `seconds`。
