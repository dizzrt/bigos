## 1. 终端边界梳理

- [x] 1.1 审查现有 TTY/console/keyboard/shell/fd 路径，确认默认控制台终端的数据流、初始化顺序和当前可复用接口。
- [x] 1.2 定义最小终端抽象的数据模型：默认终端对象、输入 record/event、输出 sink、控制字符分类和 unsupported control policy。
- [x] 1.3 明确 early diagnostic direct-output 与普通 terminal output 的边界，确认 panic、fault、smoke marker 不依赖终端初始化。

## 2. Kernel TTY 与 Console 实现

- [x] 2.1 在 kernel terminal/TTY 层加入固定容量的 printable/control input 表示，保持 IRQ producer 无分配、无阻塞、无普通 echo。
- [x] 2.2 将 newline/carriage return/backspace/delete-like、EOF-like、interrupt-like 与 unsupported control 的非中断消费语义接入默认终端路径。
- [x] 2.3 保持 TTY 阻塞/非阻塞读取语义可区分，确认 keyboard IRQ1 只执行有界 enqueue 和可选 IRQ-safe wakeup。
- [x] 2.4 将普通 terminal output 经现有 console output API 暴露给 fd/syscall 路径，避免 shell/user program 直接依赖 VGA、COM1 或 diagnostic-only API。

## 3. Userland 与 Shell 消费

- [x] 3.1 更新 `/bin/sh` 行输入处理，使 prompt、line feedback、EOF-like、interrupt-like 和 unsupported control 行为确定且有界。
- [x] 3.2 确认 shell prompt、内建命令输出、外部命令 stdout/stderr、错误信息仍通过 stdout/stderr 与现有 fd/syscall 路径输出。
- [x] 3.3 若需要新增最小 libc wrapper 或用户态辅助接口，保持 freestanding-safe、静态链接、有界 buffer，并避免 hosted libc/termios 假设。
- [x] 3.4 保持 redirection、pipe、cwd、PATH、bounded status 与父 shell fd 隔离不被终端反馈或控制字符处理破坏。

## 4. 低层安全与兼容性审查

- [x] 4.1 审查 keyboard IRQ1 handler、TTY enqueue、wakeup 和 console output 的 IRQ safety、reentrancy、硬件访问顺序与 visible failure behavior。
- [x] 4.2 审查实现未改变 boot layout、linker addresses、page-table layout、direct map、IDT vectors、syscall vector `0x80`、CR3 switching、GDT/TSS、disk layout 或 boot handoff ABI。
- [x] 4.3 审查 C++/C freestanding 边界：不使用 exceptions、RTTI、hosted libc、动态分配于 IRQ context、线程库、文件/socket/env 等宿主 OS 服务。
- [x] 4.4 审查 non-goals 未被实现或暗示：termios、session、job control、terminal process group、伪终端、SMP、动态链接、完整 POSIX shell/terminal。

## 5. Validation

- [x] 5.1 运行最窄有用的默认构建检查；若 `x86_64-elf-gcc`、`x86_64-elf-g++`、`xmake` 或磁盘镜像路径不可用，记录 blocker、替代检查和残余风险。
- [x] 5.2 对涉及 C++ source/header/build 的改动运行或记录 clang/clangd 辅助静态检查，使用接近 freestanding C++17、x86_64 target、无 exceptions、无 RTTI 的配置；区分历史诊断、当前变更新诊断和 freestanding false positives。
- [x] 5.3 运行或记录 QEMU headless 行为验证，优先复用默认 userland/userland smoke 路径观察 init/shell/user program 的确定性输出；缺失期望观察必须记为失败。
- [x] 5.4 环境允许时执行图形 QEMU 或 Bochs 手工/注入输入验证，记录 prompt、typed input feedback、control-character behavior、command output、emulator backend、display/input method 和结果。
- [x] 5.5 若改动触及 IRQ、keyboard、console、port IO 或硬件行为，环境允许时记录 Bochs 或 QEMU+Bochs 交叉验证；不可用时记录 ROM/display/toolchain 缺失和 residual hardware-behavior risk。
- [x] 5.6 若运行 Python helper、pytest、ruff、pyright 或临时 Python 验证，使用 `uv run ...`；若 `uv` 不可用，明确记录 blocker，不静默改用系统 Python。

## 6. Documentation And Records

- [x] 6.1 更新相关架构或验证文档，使用仓库相对路径，保持 `docs/en` canonical 与 `docs/zh` mirror 同步。
- [x] 6.2 保持 `roadmap.md` 只描述项目级能力、缺口、规划方向和阶段性优先级，不加入源码入口、命令、marker、文件路径或验证日志。
- [x] 6.3 在 validation notes 中区分已通过检查、跳过/阻塞检查、替代检查、历史诊断、当前变更新问题和残余风险。
