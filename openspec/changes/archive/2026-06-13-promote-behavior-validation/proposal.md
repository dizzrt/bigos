## Why

TTY console input capability6 需要把现有用户态、shell、进程/fd 和运行时文件系统能力从“可构建、可冒烟”推进到“行为可观察、可复现、可回归保护”。随着后续重构和 backend 解耦工作继续推进，项目需要一套优先面向运行时行为的验证基线，避免只依赖标记或人工观察导致行为退化被漏检。

## What Changes

- 将行为导向验证提升为最小可用系统的首选验证方向，覆盖交互式 shell、简单 C 程序、进程/fd 语义、文件系统操作和用户态兼容性。
- 建立分层验证策略：源码/spec 一致性、构建/打包检查、QEMU headless 可观察行为、以及可选的图形/Bochs/人工交互证据各自有明确职责。
- 要求行为断言使用运行时可判定结果，例如 shell 输出、程序 stdout/stderr、退出状态、文件内容、fd/pipe 端点效果或串口/日志观测。
- 要求环境依赖项缺失时记录 skipped/blocked、替代检查和残留风险，而不是把未运行的 emulator/hardware 检查记为通过。
- 保持边界：不引入完整 POSIX、作业控制、动态链接、SMP、持久完整可写文件系统、新存储驱动、新 boot backend 或强制每次变更运行图形/硬件验证。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `runtime-smoke-validation`: 将运行时 smoke 矩阵升级为行为导向验证矩阵，要求 shell、简单 C 程序、文件系统和用户态兼容性检查具备可观察断言、失败判定和环境缺失记录。
- `posix-like-process-io-subset`: 收紧进程/fd、pipe/redirection、exec/wait 和用户可见错误路径的组合行为验证要求，确保行为结果可通过输出、退出状态、文件/fd 效果或确定性日志判定。

## Impact

- Affected subsystems: userland/runtime validation, `/bin/sh` validation paths, simple user C programs, process/fd syscall behavior, VFS/runtime filesystem behavior, QEMU/Bochs validation documentation.
- Architecture assumptions: 当前可运行 backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；该 change 不要求新增 UEFI、第二架构 backend 或修改 boot handoff。
- Memory/layout assumptions: 不改变内核链接地址、boot 地址、IDT/syscall vector、CR3 切换、page-table layout、disk layout 或 ABI；验证只观察现有边界内行为。
- Emulator/toolchain assumptions: `x86_64-elf-gcc`、xmake、QEMU headless 是优先自动化路径；Bochs、图形显示、键盘输入或硬件式交互证据保持分层可选。
- Documentation impact: 后续实现需要同步 docs/en 与 docs/zh 中的验证说明，并保持 roadmap 只描述规划级状态。
