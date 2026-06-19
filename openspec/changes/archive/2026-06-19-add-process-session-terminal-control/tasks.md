## 1. 进程归属核心

- [x] 1.1 审查现有 `Process`、PID registry、`fork`、`execve`、exit/reap 和 wait 路径，确认 `pgid/sid` 状态插入点、锁/中断上下文边界和 PID reuse 风险。
- [x] 1.2 为用户进程增加有界 `pgid` 与 `sid` 状态，初始化 init/shell 归属，并保持 freestanding-safe、无 hosted runtime 假设。
- [x] 1.3 实现 `fork` 继承、`execve` 保持、exit/reap 清理和 group/session 查询辅助函数，确保不会留下指向已释放进程对象的引用。
- [x] 1.4 实现有界 process group/session 变更规则和错误码，包括非法 pid、非法 pgid、跨 session、目标已退出和不允许变更的确定性失败。

## 2. Syscall 与 libc wrapper

- [x] 2.1 分配并接入有限归属控制 syscall，保持 `int 0x80` ABI、IDT vector 和 syscall 入口机制不变。
- [x] 2.2 在 syscall 层实现 `pid/pgid/sid` 查询、process group 设置、session 创建和默认终端 foreground 查询/设置的用户指针与 errno 边界。
- [x] 2.3 在 freestanding libc headers/wrappers 中暴露对应接口，保证简单静态 C 程序可消费成功值和确定性 errno。
- [x] 2.4 更新用户态小型验证程序或新增 bounded helper，用于打印/检查 `pid/pgid/sid/foreground_pgid` 和失败路径。

## 3. 默认终端 foreground binding

- [x] 3.1 在默认 terminal abstraction 中增加单一 foreground `pgid` 状态，并定义初始化、查询、设置和无效化策略。
- [x] 3.2 将 foreground 设置限制在普通 syscall/用户进程上下文，避免 IRQ、scheduler-critical 或 preemption-disabled 路径执行阻塞、分配或 shell 策略。
- [x] 3.3 在进程退出/reap 后保护 terminal foreground binding，确保失效 group 查询或恢复返回确定性结果而不解引用释放对象。
- [x] 3.4 审查 terminal 输出路径，确认 foreground binding 不改变 stdout/stderr、early diagnostics、panic 或 smoke marker 的输出边界。

## 4. Shell foreground command 消费

- [x] 4.1 扩展 shell 外部命令启动路径，为单命令创建/设置 bounded foreground process group，并在 wait 完成后恢复 shell foreground binding。
- [x] 4.2 扩展 shell 单级 pipe 路径，使两端子进程共享 foreground process group，同时保留现有 pipe endpoint close、wait 和 terminal command status 语义。
- [x] 4.3 覆盖 setup failure、fork failure、exec failure、child nonzero exit 和 foreground 恢复失败路径，保证 shell stdin/stdout/stderr 与 read-parse-execute loop 保持可用。
- [x] 4.4 保持 background job、`fg`/`bg`、job table、完整 POSIX shell grammar 和 `termios` 控制为 unsupported，并输出确定性错误或 unsupported behavior。

## 5. Terminal input 与信号目标

- [x] 5.1 审查现有 keyboard IRQ producer、terminal input consumer 和 signal delivery 路径，确认 interrupt-like input 到 foreground group 的安全执行边界。
- [x] 5.2 将默认终端 interrupt-like input 对齐到当前 foreground process group 的有界信号投递或等价 terminal event，确保非前台 group 不被误投递。
- [x] 5.3 确认 IRQ 路径只执行有界 producer/wakeup 工作；若实现中存在直接投递，记录其无分配、无阻塞和 IRQ-safe 依据。
- [x] 5.4 覆盖无 foreground group、失效 foreground group、空 group 和权限拒绝场景的确定性结果。

## 6. 文档与规格边界同步

- [x] 6.1 更新相关 userland/kernel 文档，描述 bounded process group、session 与 foreground terminal binding，保持 `docs/en` 与 `docs/zh` 镜像同步。
- [x] 6.2 更新 headers 或 source-adjacent 注释中的兼容性说明，明确完整 job control、后台作业、`termios`、多终端、动态链接、SMP 和完整 POSIX libc 仍不支持。
- [x] 6.3 审查规划文档和 OpenSpec 语言，确保不引入具体 entry point、源码级实现细节、archive 索引或规划编号引用。

## 7. 构建、静态检查与运行验证

- [x] 7.1 运行 `xmake`，使用 x86_64-elf cross toolchain 验证内核与用户态构建；若 toolchain 或 xmake 不可用，在 validation notes 中记录 blocker、替代检查和剩余风险。
- [x] 7.2 运行接近 GCC cross-build 环境的 clang C++ 辅助检查，至少覆盖修改过的 `kernel/core/proc`、`kernel/core/syscall`、`kernel/core/terminal`、`kernel/core/signal` 和相关 headers；修复当前 change 引入的有效诊断。
- [x] 7.3 运行 clangd 辅助诊断或记录不可用原因，区分历史诊断、当前 change 诊断和 freestanding 配置 false positive。
- [x] 7.4 在可用环境中运行 QEMU headless 行为验证，观察 process group/session/foreground terminal binding、shell foreground command、single pipe 和 interrupt-like input 的确定性结果。
- [x] 7.5 如 QEMU、Bochs、ROM/display 依赖或 disk image 配置不可用，记录未运行项、原因、替代的 build/static checks 和剩余 runtime 风险。
- [x] 7.6 形成 validation notes，分别列出通过的检查、无法运行的检查、历史诊断、当前 change 引入并已修复的问题，以及仍需后续处理的风险。

## Validation Notes

- 通过：`xmake`（默认配置）完成 kernel 与默认 user init/shell 构建。
- 通过：`xmake f --userland_smoke=y` 后 `xmake` 完成 kernel 与 userland smoke 构建。
- 通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/process_session_terminal_userland.log --expect-serial-marker BIGOS_USERLAND_PASSED`，QEMU headless 观察到 `BIGOS_USERLAND_PASSED`；日志中仅有既有 boot assembler `movsd`/`movsl` warning。
- 通过：`uv run pytest tests/test_syscall_entry_source.py tests/test_process_lifecycle_source.py tests/test_fd_vfs_shell_source.py tests/test_signals_source.py tests/test_writable_fs_page_cache_pipe_source.py`，61 passed。
- 通过：`clang++ -std=c++17 --target=x86_64-elf -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -DBIGOS_USER_PROCESS -DBIGOS_USERLAND_SMOKE -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/proc/proc.cc kernel/core/syscall/syscall.cc kernel/core/terminal/tty.cc kernel/core/signal/signal.cc`。
- 通过：`x86_64-elf-gcc` 对 `user/sh/sh.c` 与 `user/smoke/userland_smoke.c` 执行 `-std=c17 -Wall -Wextra` 编译检查。
- 已记录历史/非本 change 诊断：包含 `tests/test_tty_console_input_source.py` 的组合 pytest 有 2 个 source-contract 失败，分别期待旧 `char buffer[TTY_INPUT_CAPACITY];` 与旧 CR3 helper 字符串；当前 `xmake` 与针对本 change 的源码检查均通过。
- 已记录 clangd 辅助诊断：`clangd --check` 可运行并成功构建 AST，但 Apple clangd check mode 对 `proc.cc`、`syscall.cc`、`tty.cc` 输出 tweak self-test failure（如 `AddUsing` / `ExtractFunction`），未形成编译诊断；以 clang++ `-fsyntax-only` 与 `xmake` 作为有效替代检查。
- 未运行：Bochs cross-validation；本 change 已用 QEMU headless 覆盖默认 x86_64 Legacy BIOS 路径，Bochs 不作为本次完成条件。
