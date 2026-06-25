## 1. Syscall ABI 与内核入口

- [x] 1.1 在 `include/bigos/syscall.h` 追加 `SYS_SLEEP_MS` 编号、参数单位、返回语义和非目标注释，确认所有既有 syscall 编号不变。
- [x] 1.2 在 `kernel/core/syscall/syscall.cc` 增加 `sys_sleep_ms(milliseconds)` helper，完成零时长、接近 tick deadline 安全上限的最大时长、毫秒到 tick 向上取整、乘法/加法溢出和 `sched::can_block()` guard。
- [x] 1.3 将 `SYS_SLEEP_MS` 接入 syscall dispatcher，保持 `int 0x80` ABI、返回值写回 `rax` 和 no-EOI 规则不变。
- [x] 1.4 将 scheduler 内部返回值映射为用户可见结果：正常 timeout completion 返回 0，forbidden blocking 返回 `-EWOULDBLOCK`，非法/超界参数返回 `-EINVAL`，不向用户态泄漏 `WAIT_TIMEOUT` 或 `WAIT_BLOCK_FORBIDDEN`。

## 2. Timer/Scheduler 边界复核

- [x] 2.1 复核 `timer::sleep_for()` / `sched::sleep_for()` 调用前置条件，确认 sleep syscall 只从普通可阻塞 syscall 上下文进入。
- [x] 2.2 复核 tick deadline 计算不会因 `timer::ticks() + ticks` 溢出回绕；增加局部 helper 或常量按当前 tick 安全余量限定 `SYS_SLEEP_MS_MAX`。
- [x] 2.3 确认实现不在 IRQ/timer tick 路径增加动态分配、文件/块 IO、用户内存访问、PIC EOI 或 hosted runtime 依赖。

## 3. 用户态 libc 接口

- [x] 3.1 在 `user/libc/include/sys_nr.h` 追加 `SYS_SLEEP_MS`，并与内核 syscall 编号保持一致。
- [x] 3.2 在 `user/libc/syscall.c` 增加 `bigos_sleep_ms(unsigned long milliseconds)` wrapper，使用现有 errno translation 约定。
- [x] 3.3 在 `user/libc/include/unistd.h` 暴露 `bigos_sleep_ms()` 和受限 `sleep(unsigned int seconds)` 声明，并通过注释说明 bounded/非完整 POSIX 语义。
- [x] 3.4 在 `user/libc/syscall.c` 实现 `sleep(seconds)`，处理 seconds-to-milliseconds 溢出，并通过 `get_tick()` 前后差值估算已睡整秒数后返回剩余秒数；仍不承诺完整 POSIX signal interruption 语义。
- [x] 3.5 按需更新 `user/libc/include/time.h` 注释，避免继续声明“没有 sleep”或误导为完整 POSIX timer API。

## 4. Runtime Smoke 与构建集成

- [x] 4.1 新增用户态 sleep smoke 程序，读取 `get_tick()`，调用 `bigos_sleep_ms()`，验证 tick delta 不低于向上取整后的预期 tick 数。
- [x] 4.2 为 sleep smoke 增加默认关闭 xmake 配置和构建/打包入口，成功输出 `BIGOS_SLEEP_SYSCALL_PASSED`，失败输出 `BIGOS_SLEEP_SYSCALL_FAILED`。
- [x] 4.3 更新 `tools.bigosdev` runtime smoke matrix，将 sleep syscall case、xmake switch、expected marker、timeout 和日志路径纳入可选矩阵；Python 变更使用 `uv run ...` 验证。
- [x] 4.4 若未修改 Python tooling，则在验证记录中说明 runtime smoke case 可通过现有 helper 手动运行或后续矩阵扩展接入。

## 5. 文档与 OpenSpec 同步

- [x] 5.1 更新相关用户态 header 注释或文档，说明 `bigos_sleep_ms()` 是 coarse tick-based bounded sleep。
- [x] 5.2 如修改 `docs/en`，同步更新匹配的 `docs/zh` 路径；若本变更只更新源码注释和 OpenSpec，则记录 docs runtime 文档不适用。
- [x] 5.3 搜索并清理与 sleep/time API 边界冲突的说明，确保不声称完整 POSIX `nanosleep`、`alarm`、timerfd、高精度 timer 或 signal-interruptible sleep。

## 6. 验证

- [x] 6.1 运行 OpenSpec 校验，确认 `add-blocking-sleep-syscall` 的 proposal、design、specs 和 tasks 可解析。
- [x] 6.2 运行源码级 ABI/编号一致性检查，覆盖 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 的 `SYS_SLEEP_MS` 镜像关系和 append-only 约束。
- [x] 6.3 运行窄范围 xmake/cross-toolchain 构建；若 `x86_64-elf-gcc`、xmake 或依赖缺失，记录 blocker、替代检查和剩余风险。
- [x] 6.4 对新增/修改 C++ 源和头运行贴近 GCC cross-build 的 clang/clangd 辅助诊断；修复本 change 引入的有效诊断，并区分历史诊断与 freestanding false positive。
- [x] 6.5 运行 QEMU headless sleep syscall smoke，使用 `uv run python -m tools.bigosdev run ... --expect-serial-marker BIGOS_SLEEP_SYSCALL_PASSED` 或等价矩阵 case，并将串口日志写入 `logs/`。
- [x] 6.6 如本地 Bochs、ROM/display 或 serial oracle 可用，针对 syscall/timer/scheduler 交互运行 Bochs 交叉验证；不可用时记录跳过原因和剩余低层风险。
