## 1. Terminal Mode 内核状态

- [x] 1.1 梳理 `kernel/core/terminal/tty.cc`、`include/bigos/tty.h`、`kernel/core/syscall/syscall.cc` 的默认 fd `0` 输入路径，记录 canonical 现有行为和 source tests 覆盖点。
- [x] 1.2 增加固定大小 `TerminalInputMode` / mode state，初始化为 canonical，并提供 kernel-internal getter/setter。
- [x] 1.3 实现 mode 请求校验：只接受 canonical/raw 和已知 flag/version/size，非法请求返回确定性错误且不改变旧 mode。
- [x] 1.4 确认 mode state 不依赖动态分配、文件系统、用户态进度或显示 backend 状态。

## 2. TTY 输入语义

- [x] 2.1 将 fd `0` 默认 read 路径改为 mode-aware：canonical 保持现有行为，raw mode 逐字节或按当前可用字节返回。
- [x] 2.2 raw mode 下禁用 ordinary echo、canonical line-end 等待、EOF-like empty-read 转换和 Ctrl-C 自动 foreground-group signal。
- [x] 2.3 raw mode 下定义并实现导航/控制事件的用户态交付策略，确保不把用户态应接管事件固定消费为 scrollback。
- [x] 2.4 处理 TTY buffer 容量不足和 mode 切换时未读输入策略，保证 ring indices、dropped counter、wait queue 和 keyboard decoder state 不损坏。

## 3. Syscall 与权限边界

- [x] 3.1 以 append-only 方式增加 terminal mode syscall number、内核 dispatch 和公共 syscall header 声明，保持既有 syscall 号位、寄存器 ABI、`VECTOR_SYSCALL` 和 no-EOI 语义不变。
- [x] 3.2 实现 terminal mode query syscall，验证用户输出缓冲或 register-return contract，并确保查询不修改 terminal/process/fd 状态。
- [x] 3.3 实现 terminal mode set syscall，检查 current process、session、foreground group 权限；shell/session leader 的恢复例外只能设置 canonical，非法调用返回确定性 errno。
- [x] 3.4 更新 source-level syscall ABI 检查，覆盖新增号位、wrapper 常量、未知 flag 拒绝和用户指针校验。

## 4. 进程生命周期与 Shell 恢复

- [x] 4.1 明确并实现 `fork` 观察同一默认 terminal mode、`execve` 保持 mode、退出/reap 不留下 dangling owner 的行为。
- [x] 4.2 在 foreground process group 消失、shell 恢复 foreground group 或 foreground command wait 完成时提供 canonical 恢复路径。
- [x] 4.3 更新 `/bin/sh` 必要路径：启动 foreground command 前后保存/恢复 terminal mode，失败路径也回到 canonical。
- [x] 4.4 验证 raw mode 程序异常退出、exec 失败、foreground setup 失败后 shell 仍可继续读取命令。

## 5. 用户态 libc 与测试程序

- [x] 5.1 增加 BigOS-specific terminal mode 头文件常量和结构，避免声明完整 POSIX `termios`。
- [x] 5.2 增加 libc wrapper `bigos_tcgetmode` / `bigos_tcsetmode`，按现有 errno 翻译约定返回。
- [x] 5.3 增加最小用户态测试程序或 userland smoke 场景，覆盖查询 mode、切 raw、读 raw 字节、恢复 canonical。
- [x] 5.4 确认用户态公共头不暴露未实现的 baud rate、`VMIN/VTIME`、pseudo-terminal、完整 `tcgetattr/tcsetattr` 语义。

## 6. 文档与 OpenSpec 同步

- [x] 6.1 更新 `docs/en/arch/tty-console-input.md`，说明 BigOS-specific canonical/raw mode、权限边界、raw mode 非目标和 shell 恢复策略。
- [x] 6.2 同步更新 `docs/zh/arch/tty-console-input.md`，保持技术事实与英文文档一致。
- [x] 6.3 更新相关 syscall/userland runtime 文档，记录新增 terminal mode syscall/libc wrapper 与 append-only ABI 边界。
- [x] 6.4 检查文档、headers、OpenSpec 文本不得声明完整 POSIX `termios`、多 TTY、pseudo-terminal、后台读写控制或完整 job control。

## 7. Validation

- [x] 7.1 运行相关 source-level tests，至少覆盖 TTY mode state、raw/canonical read、syscall number/header、libc wrapper 和 shell 恢复路径；Python 测试命令使用 `uv run pytest ...`，若 `uv` 不可用则记录 blocker 和剩余风险。
- [x] 7.2 运行默认 `xmake` 构建，确认 `x86_64-elf-gcc/x86_64-elf-g++` 工具链可用；若不可用则记录缺失工具和未验证风险。
- [x] 7.3 对修改的 C++ 源和头文件执行 clang 辅助检查，尽量使用 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include path；区分历史诊断、当前变更诊断和 freestanding false positives。
- [x] 7.4 对修改的 C++ 源和头文件执行 clangd 辅助诊断或记录 clangd 配置不可用原因；clang/clangd 不替代 xmake cross build。
- [x] 7.5 运行 QEMU headless smoke，优先通过 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/... --expect-serial-marker BIGOS_USER_EXEC` 或等价项目命令确认默认用户态仍可启动。
- [x] 7.6 在可用环境中运行 terminal raw mode runtime smoke，观察 raw mode 不等 Enter 返回、Ctrl-C 不自动 signal、恢复 canonical 后 shell 可继续交互；若 QEMU/Bochs、显示、ROM、磁盘镜像或串口 oracle 不可用，记录跳过原因和剩余风险。
- [x] 7.7 汇总 validation notes，分开列出通过项、无法运行项及原因、历史诊断、当前变更新增问题和残余风险。

## Validation Notes

- 通过：`uv run pytest tests/test_fd_vfs_shell_source.py tests/test_tty_console_input_source.py tests/test_syscall_entry_source.py`，53 passed。
- 通过：默认 `xmake` 构建，确认 cross toolchain 可用。
- 通过：`clang++ --target=x86_64-elf -fsyntax-only -std=c++17 -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_USER_PROCESS -mno-sse -mno-sse2 -mno-mmx -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -DNDEBUG kernel/core/terminal/tty.cc kernel/core/syscall/syscall.cc`。
- 部分通过：`clangd --check` 能读取 `.clangd` 与 `compile_commands.json` 并完成 AST 构建；Apple clangd check-mode 在 tweak 自测阶段返回 `ExtractFunction` / `AddUsing` 错误，未观察到当前变更相关语义诊断，记录为工具 check-mode false positive。
- 通过：`uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/add-terminal-raw-mode-default-serial.log --expect-serial-marker BIGOS_USER_EXEC`。
- 通过：`xmake f --userland_smoke=y && uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/add-terminal-raw-mode-userland-smoke-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`；随后已执行 `xmake f --userland_smoke=n` 恢复默认配置。
- OpenSpec：`openspec validate add-terminal-raw-mode --strict` 通过。
- 未完全自动化：当前 headless helper 没有键盘输入注入/串口 oracle 覆盖真实交互 raw read，所以“raw mode 下不等 Enter 返回”和“Ctrl-C 不自动 signal”的实机键盘观察未单独运行；本次通过 source-level checks 与 userland smoke 覆盖 raw/canonical mode state、ABI、非法请求保持旧 mode、默认启动和 shell 恢复路径，剩余风险是 PS/2 实际按键注入到 raw read 的端到端证据不足。
