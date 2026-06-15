## 1. 规格与边界确认

- [x] 1.1 对照 `roadmap.md` Stage 42 和本 change specs，确认实现范围只覆盖有界进程、默认终端和 shell 组合行为。
- [x] 1.2 审查现有 `process-lifecycle`、`signals`、`minimal-terminal-abstraction`、`user-shell`、`posix-like-process-io-subset` 规格，确认新增需求没有扩大到 sessions、terminal process groups、job control、termios 或完整 POSIX shell。
- [x] 1.3 记录架构、地址布局和 ABI 不变性：不修改 boot handoff、linker address、page-table layout、direct map、GDT/TSS、CR3 switching、InterruptFrame、syscall vector `0x80`、disk layout 或 user ELF ABI。

## 2. 进程等待与信号状态

- [x] 2.1 梳理当前 `wait`/`waitpid` 内核路径和 libc wrapper，确认 exact PID、any-child、无匹配子进程、unsupported options 的现有行为。
- [x] 2.2 实现或修复 bounded `waitpid` 匹配、状态写回、错误码和 reap 交接，确保失败路径不阻塞、不回收无关进程、不污染 status storage。
- [x] 2.3 对齐 normal exit、user fault、exec no-return failure 和 signal termination 的 bounded status 编码，并让 shell/libc 只消费已定义字段。
- [x] 2.4 验证 signal default Terminate 与 `SIGKILL` 通过现有 exit/fault-to-reaper 生命周期发布 waitable 状态，且不引入 process group 或 job-control 状态。

## 3. 默认终端控制输入

- [x] 3.1 审查 keyboard IRQ1 producer，确认控制输入分类不分配内存、不阻塞、不执行 echo、不调用 filesystem、不执行 shell policy。
- [x] 3.2 实现或修复非 IRQ terminal consumer 对 newline/carriage return、backspace/delete-like、EOF-like、interrupt-like 和 unsupported control bytes 的有界处理。
- [x] 3.3 确认可见反馈通过普通 terminal output/fd 路径发生，early diagnostics、panic 和 smoke marker 仍独立于终端初始化和 shell 进度。
- [x] 3.4 记录 interrupt-like input 的选定策略：line cancellation、bounded signal action 或 documented no-op，并说明为何不需要 process group/session/job control。

## 4. Shell 交互与 fd 隔离

- [x] 4.1 梳理 `/bin/sh` 解析、内建、外部命令、PATH、cwd、pipe、redirection 和 wait 组合路径，标出所有失败恢复点。
- [x] 4.2 实现或修复 last-command bounded status，覆盖 builtin、外部命令、exec 失败、unsupported syntax、redirection failure、pipe failure 和 signal-terminated child。
- [x] 4.3 实现或修复 pipe/redirection setup 的父 shell fd 隔离，确保临时 fd 在失败路径关闭，父 shell stdin/stdout/stderr 在成功和失败后都可继续使用。
- [x] 4.4 实现或修复 shell 对 EOF-like 与 interrupt-like terminal results 的有界消费，确保 prompt/read loop 可恢复或按文档退出。
- [x] 4.5 检查路径工具和简单用户程序在 pipe、redirection、cwd/PATH 和 errno/exit-status 组合下仍保持 deterministic 输出和失败行为。

## 5. 文档与用户可见契约

- [x] 5.1 更新相关规格或文档说明，使用“bounded POSIX-like subset”描述 Stage 42 行为，不声明完整 POSIX process、terminal、shell、libc 或 filesystem。
- [x] 5.2 若修改 `docs/en` 或 `docs/zh`，同步更新对应语言镜像并保持相同相对路径结构。
- [x] 5.3 若需要更新 `roadmap.md`，只保留项目规划层级描述，不写入具体入口点、文件路径、命令、validation marker、源码实现细节或 archive/version 索引。

## 6. 构建、静态检查与验证

- [x] 6.1 运行最窄有用的 `xmake` 构建，记录 `x86_64-elf-gcc`/`x86_64-elf-g++`、xmake 和 disk image 依赖是否可用。
- [x] 6.2 对 C/C++ 运行辅助 clang/clangd 静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include path、no exceptions、no RTTI，并区分历史诊断、当前 change 诊断和 freestanding false positives。
- [x] 6.3 对 IRQ/terminal 改动执行源级或静态检查，确认 keyboard IRQ producer 不调用动态分配、blocking wait、filesystem、hosted runtime、ordinary echo 或 shell policy。
- [x] 6.4 在环境可用时运行 QEMU headless 或图形路径，验证 shell-launched child normal exit、deterministic command failure、wait-status recovery、pipe/redirection fd isolation 和 shell prompt recovery。
- [x] 6.5 在环境可用时用 QEMU 或 Bochs 控制台输入观察 newline、backspace/delete-like、EOF-like 或 interrupt-like 行为；不可用时记录缺失的 emulator、ROM/display、console input、timeout oracle 或工具链依赖。
- [x] 6.6 如修改 Python helper 或验证脚本，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`；若未修改 Python 文件，记录 Python 验证不适用。
- [x] 6.7 编写验证记录，分离 passed checks、skipped checks 及原因、替代检查、历史诊断、当前 change 引入问题和剩余风险。
