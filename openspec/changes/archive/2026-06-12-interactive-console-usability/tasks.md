## 1. 交互路径梳理

- [x] 1.1 审查 default boot 中 resident init、`/bin/sh`、stdin/stdout/stderr、TTY input 和 console output 的连接关系，记录当前缺口。
- [x] 1.2 确认本 change 不需要修改 boot layout、linker address、IDT/syscall vector、CR3 切换、disk layout 或用户 ABI；如发现必须修改，先更新 design 并重新评审。
- [x] 1.3 明确 shell 判断交互式 TTY/console 的最小策略，避免 prompt 污染重定向、管道或非交互执行路径。

## 2. Kernel TTY / Console

- [x] 2.1 补齐默认 TTY/console 到用户态 fd 的读写承载路径，使 shell stdin 能读取键盘输入，stdout/stderr 能显示到文本控制台。
- [x] 2.2 确保 keyboard IRQ1 handler 仍只执行有界 scancode 转换、TTY enqueue 和允许的 bounded wakeup，不直接写普通 VGA/serial 回显。
- [x] 2.3 在非中断消费路径实现或修正 printable、newline、backspace 的有界输入反馈，保持 allocation-free/非阻塞中断边界。
- [x] 2.4 审查控制台输出失败和缓冲满的行为，确保不会 panic、越界写、睡眠于 IRQ 上下文或依赖 hosted runtime。

## 3. Userland Shell

- [x] 3.1 为 `/bin/sh` 的交互式路径补齐 deterministic bounded prompt，并在读取下一行前可见输出。
- [x] 3.2 确保 shell readline、输入回显、内建命令输出、外部命令 stdout/stderr 和可恢复错误都经现有用户态 fd/syscall 路径输出。
- [x] 3.3 保持 shell 行长、argv、管道段数和路径拼接上界，超界时输出确定性错误并回到 prompt/read 循环。
- [x] 3.4 验证重定向和管道路径不被交互 prompt 破坏，非交互 stdin/stdout 不被误判为默认 console session。

## 4. Source And Static Checks

- [x] 4.1 增加或更新源码级检查，覆盖 keyboard ISR 不直接调用普通 console/serial 输出、allocation、blocking wait、filesystem 或 `mdelay()`。
- [x] 4.2 增加或更新 shell/TTY 行为检查，覆盖 prompt 输出、输入边界、回显来源、stdout/stderr 可见性和错误恢复。
- [x] 4.3 对涉及的 C++ 源码和头文件运行辅助 clang/clangd 检查；记录历史诊断、当前 change 新增诊断、freestanding 配置误报和无法运行原因。
- [x] 4.4 修复当前 change 引入的 clang/clangd 有效错误或警告；若辅助工具无法等价配置 x86_64 freestanding C++17 环境，记录剩余风险。

## 5. Build And Runtime Validation

- [x] 5.1 运行最窄有用的 xmake 默认构建或等价 x86_64-elf GCC cross-build，确认 normal boot/userland 相关对象可构建。
- [x] 5.2 通过 QEMU headless 串口/日志路径验证默认 init/userland 行为断言仍可观察，记录命令、日志路径、结果和失败信息。
- [x] 5.3 在可用的图形 QEMU、Bochs、手动键盘或 emulator input 环境中验证 prompt、输入回显和命令输出可见，记录 backend、输入方法、执行命令、观察输出和结果。
- [x] 5.4 若 `uv`、xmake、x86_64-elf toolchain、QEMU、Bochs、ROM/display、disk image 或 keyboard input 能力缺失，将对应检查标记为 skipped/blocked，并记录替代检查与剩余风险。

## 6. Documentation And Review

- [x] 6.1 更新必要的 docs/en 与 docs/zh 镜像文档，描述 交互控制台可用性的交互控制台边界、non-goals 和验证方式；保持 roadmap 只在项目规划层级。
- [x] 6.2 审查所有文档和注释，确认没有声称完整 POSIX terminal、termios、job control、SMP、UEFI parity、完整 libc 或持久完整可写文件系统。
- [x] 6.3 汇总验证记录，区分已通过检查、无法运行检查、历史诊断、当前 change 引入并已修复的问题，以及仍然存在的 console-usability 风险。
