## Context

BigOS 已有单核进程生命周期、`fork`/`execve`/`wait`、信号、默认控制台终端抽象和 `/bin/sh`，但这些能力仍以“每个进程独立执行”为主，缺少把一组前台子进程与默认终端绑定的控制模型。当前 terminal 规格明确不提供 session、terminal process group 或 job control，本 change 将该边界推进到一个有界、单默认终端的 foreground process group 模型。

该设计只面向当前 x86_64 Legacy BIOS/MBR/exFAT 默认运行路径。它不改变 boot handoff、link address、page-table layout、direct map、IDT/syscall vector、CR3 切换、磁盘布局或 UEFI backend 边界。

## Goals / Non-Goals

**Goals:**

- 为每个用户进程维护有界 `pgid` 与 `sid`，并定义 `fork`、`execve`、exit/reap 时的继承和清理语义。
- 在默认控制台终端中维护一个 foreground process group 绑定，使 shell 可以把外部命令或单级 pipe 临时设为前台，并在完成后恢复 shell 自身前台绑定。
- 提供有限 syscall/libc wrapper 让静态 C 程序和 shell 查询、创建、调整 process group/session/foreground terminal binding，并通过 errno 观察失败。
- 将 terminal interrupt-like input 对齐到当前 foreground process group 的有界信号投递语义，同时保留单核、同步、普通 syscall 上下文约束。
- 增加行为验证，覆盖继承、错误码、前台绑定恢复、终端输入目标和 shell 可恢复性。

**Non-Goals:**

- 不实现完整 POSIX job control、后台作业、`SIGSTOP`/`SIGCONT` 作业控制、orphaned process group 规则、`tcsetpgrp` 的完整 POSIX 语义、`termios`、多终端、伪终端或完整 shell 语言。
- 不引入 SMP、跨核信号投递、async I/O、动态链接、完整 POSIX libc、权限/credential 完整模型或新 ISA/backend。
- 不让 IRQ 路径执行 shell 策略、文件 I/O、阻塞等待或动态分配。

## Decisions

1. **在现有进程对象中保存 `pgid`/`sid`，而不是新增独立进程控制对象图。**

   - 理由：当前 BigOS 是单核、进程数量有界，进程 registry 已经是 PID 查找、wait/reap 和信号投递的中心。把 `pgid`/`sid` 作为进程状态能降低生命周期复杂度，并避免引入需要复杂引用计数的控制对象。
   - 替代方案：新增 session/process-group 对象表。该方案更接近完整 POSIX，但需要处理 orphan、leader 生命周期、跨对象引用和更多失败路径，超出本 change 的有界目标。

2. **默认终端只维护一个 foreground pgid。**

   - 理由：当前只有一个默认控制台终端抽象；一个 `foreground_pgid` 足以表达 shell 与其前台子进程组的归属切换。该字段由普通 syscall 上下文更新，读取路径可被终端输入消费。
   - 替代方案：引入 tty device 表、controlling terminal 指针和 per-session 终端列表。该方案会把多终端和完整 terminal control 拉入范围，不符合当前单终端边界。

3. **控制操作全部通过普通 syscall 上下文执行。**

   - 理由：设置 pgid/session/foreground 可能需要查找进程、检查状态和报告 errno，必须运行在可阻塞、可分配的普通用户进程上下文。keyboard IRQ 只生产有界输入事件或 interrupt-like event，不执行终端归属切换或 shell 策略。
   - 替代方案：在 terminal IRQ 路径中直接处理前台组状态。该方案会违反 IRQ producer 与非中断 consumer 分层，也增加不可阻塞上下文风险。

4. **terminal interrupt-like input 以 foreground pgid 为目标执行有界信号投递。**

   - 理由：当前信号系统已有 per-process pending/mask 和 `kill` 权限边界。把默认终端的 interrupt-like input 转为面向 foreground pgid 的有界投递，可以让前台命令被中断，同时仍不实现完整 job control。
   - 替代方案：继续让 shell 自行把 control input 解释为 no-op 或取消当前行。该方案简单，但不能支撑前台命令组交互目标。

5. **shell 只消费 foreground command 语义，不声明后台作业。**

   - 理由：shell 现有模型已支持外部命令、wait、redirection 和单级 pipe。本 change 只在启动外部命令/pipe 前设置子进程组并切换前台，完成后恢复 shell pgid；不增加 `&`、`fg`、`bg`、job table 或脚本变量。
   - 替代方案：一次性实现完整 job table 和后台/前台切换。这会扩大 shell grammar、信号和 terminal 状态机，风险过大。

## Risks / Trade-offs

- [Risk] pgid/sid 生命周期与 PID reuse 交错可能导致前台绑定指向已退出组。  
  Mitigation: terminal foreground pgid 必须在设置时验证目标存在，进程退出/reap 时清理或使失效状态可检测；shell 恢复前台失败必须报告确定性错误并保持可用。

- [Risk] terminal interrupt-like input 与已有信号投递路径耦合后增加不可阻塞上下文压力。  
  Mitigation: IRQ 只记录事件或唤醒 reader，面向 foreground pgid 的信号投递在非中断上下文执行；若实现选择直接从安全边界投递，必须证明无分配、无阻塞且只使用已有 IRQ-safe 路径。

- [Risk] syscall wrapper 名称看起来接近 POSIX，容易被误读为完整兼容。  
  Mitigation: specs、headers、docs 和 validation notes 必须持续标注 bounded BigOS subset，不声明完整 `setsid`/`setpgid`/`tcsetpgrp`/job control 语义。

- [Risk] shell 前台绑定恢复失败会影响交互可用性。  
  Mitigation: shell 必须在 child completion、setup failure 和 partial pipe failure 路径尝试恢复自身 foreground pgid，失败时给出确定性诊断并保留 stdin/stdout/stderr 可用。

## Migration Plan

1. 扩展进程状态与 registry 查询能力，先保证默认 init/shell 进程拥有稳定 pgid/sid。
2. 增加 syscall 与 libc wrapper，使独立验证程序可观察 pgid/sid 和错误码。
3. 扩展默认终端 foreground pgid 状态，再接入 shell 外部命令和单级 pipe 的前台绑定恢复。
4. 将 terminal interrupt-like input 与 foreground pgid 信号投递对齐。
5. 补充行为验证和文档边界说明。

回滚策略是按子系统撤销：保持新增 syscall 默认返回 `-ENOSYS` 或不暴露 wrapper，shell 退回无前台绑定的现有执行模型，terminal interrupt-like input 退回现有有界行为。

## Resolved Decisions

- 验证路径增加专用小型静态用户程序，用于打印或断言 `pid/pgid/sid/foreground_pgid`。该工具服务默认关闭的验证路径，不作为完整 POSIX utility，也不替代现有 userland smoke；现有 smoke 可继续覆盖组合行为。
- 默认终端的 interrupt-like input 固定映射到当前已有的 interrupt signal 语义，并以当前 foreground process group 为目标。shell 只负责设置和恢复 foreground pgid，不模拟 signal delivery；无有效 foreground group 时返回确定性 no-op 或错误结果，且不得 panic 或污染进程、信号、终端状态。
