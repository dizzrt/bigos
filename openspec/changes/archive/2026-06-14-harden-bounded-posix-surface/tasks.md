## 1. 契约与现状确认

- [x] 1.1 对照 `include/bigos/syscall.h`、`user/libc/include/sys_nr.h`、`user/libc/syscall.c` 确认 bounded POSIX-like surface 复用现有 syscall number，不新增或重排 syscall ABI。
- [x] 1.2 梳理 `kernel/core/signal/**`、`kernel/core/proc/**`、`kernel/core/syscall/**` 中 signal frame、wait status、errno 返回和用户内存校验的现有行为。
- [x] 1.3 梳理 `user/sh/**` 和 `user/bin/**` 中 pipe、redirection、PATH lookup、错误输出和 status 约定，标记需要硬化的失败路径。

## 2. User Libc 表层

- [x] 2.1 新增或更新最小 `signal.h`，声明 supported signal constants、`sigset_t`、`struct sigaction`、`sigaction`、`sigprocmask`，并明确 bounded subset 注释。
- [x] 2.2 在 libc syscall wrapper 中接入 `SYS_SIGACTION`、`SYS_SIGPROCMASK`，保持负 errno 到 `errno` 的统一翻译。
- [x] 2.3 实现或接入用户态 signal trampoline，使 handler 正常返回时通过 `SYS_SIGRETURN` 恢复 interrupted user context。
- [x] 2.4 调整 `sys/wait.h` 与 wrapper，提供 bounded `wait(int *status)`、`waitpid(pid_t pid, int *status, int options)`，并保留兼容的 BigOS-specific raw wait helper。
- [x] 2.5 新增或更新 `time.h`/wrapper，使 `time` 基于现有 `SYS_GET_TIME`，支持非空输出指针并保持秒级 bounded 语义。
- [x] 2.6 新增或更新 `strerror`、`perror` 及声明，覆盖当前公开 errno 常量、未知错误 fallback 和 stderr 输出路径。

## 3. Kernel 对齐与失败行为

- [x] 3.1 检查 `SYS_SIGACTION`、`SYS_SIGPROCMASK`、`SYS_SIGRETURN` 的用户结构布局、copy-in/copy-out、mask 更新和旧值写回是否匹配新 libc 表层。
- [x] 3.2 检查 invalid signal frame、非法 signal number、非法 mask operation 的失败行为，确保不会恢复 corrupted user context。
- [x] 3.3 检查 `SYS_WAIT` 对任意子进程、指定子进程、status 写回、无子进程和用户指针错误的行为，确保能支撑 `wait`/`waitpid` wrapper。
- [x] 3.4 如果需要微调 kernel C++ 实现，保持 `int 0x80`、InterruptFrame、syscall number、user CR3 和 VMA-backed user memory validation 边界不变。

## 4. Shell 与工具硬化

- [x] 4.1 硬化 shell redirection setup，确保 open/dup2/close 任一步失败后父 shell 的 stdin/stdout/stderr 保持可用。
- [x] 4.2 硬化单级 pipe 执行，确保父子进程关闭不用的 pipe endpoints，右侧命令能观察 EOF，pipeline status 来自右侧命令。
- [x] 4.3 统一 shell 对 command-not-found、exec failure、unsupported syntax、parse error、redirection failure 和外部命令非零退出的诊断与 bounded status。
- [x] 4.4 更新 `/bin/*` 工具的错误输出，优先使用 `perror`/`strerror` 或稳定错误文本，不再只依赖裸 `errno=<n>`。

## 5. 验证

- [x] 5.1 扩展源级契约检查，覆盖 syscall mirror、errno mirror、signal constants/layout、wait constants/header 和新增 wrapper 声明。
- [x] 5.2 扩展 signal runtime smoke，覆盖 handler 安装、signal delivery、handler return through sigreturn、mask 旧值写回和默认终止路径。
- [x] 5.3 扩展 userland runtime smoke，覆盖 `wait`/`waitpid` status、unsupported wait options、`time`、`strerror`、`perror`、shell redirection 失败恢复、single-pipe EOF 和 bounded status。
- [x] 5.4 运行最窄有用构建检查；如涉及 C++ kernel 文件，额外运行或记录 clang/clangd 辅助诊断，并区分历史诊断与本 change 新增诊断。
- [x] 5.5 在环境具备 `x86_64-elf-gcc`、xmake、QEMU/Bochs 和磁盘镜像路径时运行对应 default-off smoke；若无法运行，记录缺失工具、跳过原因和剩余风险。

## 6. 文档与边界

- [x] 6.1 更新相关架构或用户态文档时保持 `docs/en` canonical 与 `docs/zh` mirror 同步，并使用 repository-relative 路径。
- [x] 6.2 在文档和 headers 中明确 bounded POSIX-like surface 仍不支持完整 POSIX libc、job control、terminal process groups、termios、dynamic linking、broad mmap、SMP、新 ISA 或持久完整 writable filesystem。
- [x] 6.3 记录验证结果，分别列出已通过检查、未运行检查及原因、历史诊断、当前 change 引入并已修复的问题。

## 验证记录

- 已通过：`uv run pytest tests/test_user_c_baseline_source.py tests/test_signals_source.py tests/test_time_and_identity_source.py`（33 passed）。
- 已通过：`xmake f --userland_smoke=y && xmake`。
- 已通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`。
- 已通过：`xmake f --signal_smoke=y && xmake && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/signal-serial.log --expect-serial-marker BIGOS_SIGNAL_PASSED`。
- 已通过：`GetDiagnostics` 未报告新增诊断。
- 历史诊断：包含 `tests/test_fd_vfs_shell_source.py` 的 broader fd/VFS source-contract 检查仍引用已缺失的 `openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md`，并存在与本 change 无关的 stale kernel string expectation；本 change 未将其作为通过项。
- 当前 change 修复：QEMU `userland_smoke` 初次暴露的 signal trampoline return 故障已通过同步 iret tail 中的真实 user `rsp/ss` 修复，并由 `BIGOS_USERLAND_PASSED` 验证。
