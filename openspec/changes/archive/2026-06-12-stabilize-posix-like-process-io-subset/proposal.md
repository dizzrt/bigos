## Why

BigOS 已具备有界用户态、进程生命周期、`execve`、`fork`/COW、signals、time/identity、fd/VFS、pipe/dup 和交互式 shell，但这些能力需要被整理为一个明确的 POSIX-like 进程与 I/O 子集契约，避免后续工作误把当前 baseline 解读为完整 POSIX。

当前阶段适合把分散在内核、用户态 libc、shell 和运行时验证中的行为收敛为可说明、可验证、可维护的组合边界，为后续运行时文件系统可用性、架构解耦和行为导向验证提供稳定基础。

## What Changes

- 新增 `posix-like-process-io-subset` capability，定义 BigOS 当前支持的有界 UNIX-like 进程与 I/O 行为集合。
- 明确进程生命周期、镜像替换、等待、fd 继承、标准 fd、pipe、duplication、redirection、signals、time/identity 和 shell 命令执行之间的组合语义。
- 要求简单静态 C 程序和默认 shell 路径可以依赖这些行为的成功/失败返回、错误传播和可观察输出。
- 将早期 POSIX-like 兼容性表述细化为 bounded process/I/O subset，而不是完整 POSIX 进程模型。
- 保持非目标：不引入 session、terminal process group、job control、完整权限模型、完整 POSIX process model、动态链接、完整 POSIX libc、SMP、async I/O 或 broad file-backed `mmap`。
- 保持当前运行假设：x86_64 Legacy BIOS/MBR/exFAT 路径、单核、有界用户态、静态链接用户程序、现有 `int 0x80` syscall ABI 和现有用户 ELF 装载模型。

## Capabilities

### New Capabilities

- `posix-like-process-io-subset`: 定义 BigOS 有界 POSIX-like 进程与 I/O 子集的组合行为、边界、错误语义和验证要求。

### Modified Capabilities

无。

## Impact

- Affected subsystem: kernel process model, syscall layer, fd/VFS, pipe/dup, signals, time/identity, freestanding userland libc, PID-1 init, `/bin/sh`, and small packaged user programs.
- Affected behavior: 进程创建与退出、`execve` 镜像替换、等待/回收、fd 继承和重定向、pipe 数据流、dup/dup2、signal delivery、time/identity 查询、shell 命令执行和错误报告的组合可观察行为。
- Dependencies: 依赖现有 x86_64 `int 0x80` syscall ABI、单核调度、VMA-backed 用户内存校验、fd/VFS、RAM-backed `/rw`、page/buffer cache、最小 libc wrapper、静态用户程序构建与默认 PID-1 init/shell 路径。
- Assumptions: 不改变 boot 地址、链接地址、IDT/syscall vector、页表布局、磁盘布局、用户 ELF 装载 ABI、CR3 切换假设或硬件 backend；验证仍以当前 Legacy BIOS/MBR/exFAT 镜像和可用 emulator/toolchain 为基础。
- Validation scope: 优先通过源码/构建检查、OpenSpec 校验、行为断言和分层 emulator smoke 覆盖；依赖 QEMU、Bochs、交叉工具链或本地磁盘镜像配置的检查不可用时必须记录跳过原因和残余风险。
