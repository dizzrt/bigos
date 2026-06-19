## Why

BigOS 已经具备 bounded libc foundation，但要让更多可移植小程序直接编译运行，还需要把 libc 从“基础可用”推进到“常见小程序可依赖”的成熟子集。现在应在不扩大为完整 POSIX/hosted libc 的前提下，收敛头文件兼容边界、补齐高频 C/POSIX-like helper，并用代表性用户程序验证其可移植性。

## What Changes

- 扩展 freestanding-safe 的 libc 公共头文件集合，覆盖可移植小程序常见的基础类型、宏、ctype、time/assert、stdlib/string/stdio 声明、文件/进程 wrapper 声明，并确保每个 public declaration 都有实现或明确的非支持边界。
- 补齐或稳定高频 C 库行为，优先覆盖 `ctype` 分类/转换、`strtoul`/`strtoull` 类无符号转换、`snprintf` 常见格式细节、`perror`/错误文本，以及 `strchr`、`strrchr`、`strstr`、`memchr` 这类无隐藏状态 search helper。
- 整理 POSIX-like wrapper 的用户态消费面，使小程序通过 libc 访问已实现的文件、目录、wait、time、identity、cwd、pipe/dup 与错误报告能力，而不是依赖 raw syscall 或私有声明。
- 增加代表性 portable-small-program 验证路径，覆盖头文件可编译性、静态链接、参数/环境、stdio/stderr、errno、文件/目录、数值解析、ctype、time/assert 和 wrapper 组合行为。
- 同步用户态运行时文档，继续使用 bounded C library subset / bounded POSIX-like wrapper subset 表述，避免暗示完整 POSIX libc、动态链接、共享库、locale、线程、完整 `FILE` 流或广泛 POSIX 兼容。

## Capabilities

### New Capabilities

- `portable-libc-subset`: 定义 BigOS 面向可移植小程序的 bounded libc 成熟子集，包括公共头文件兼容边界、高频 C 库 helper、POSIX-like wrapper 消费面、非目标和分层验证要求。

### Modified Capabilities

- `user-libc-min`: 将既有最小 libc / bounded libc foundation 的要求扩展到更成熟的可移植小程序子集，补充 ctype、无符号转换、错误文本、formatter 细节、头文件边界和行为验证要求。
- `user-program-build`: 要求代表性小型 C 程序继续通过现有 freestanding 静态 ELF64 构建与打包路径验证 libc 成熟子集，不依赖宿主 libc、动态链接器或共享库。
- `posix-like-process-io-subset`: 明确 libc 成熟化只是现有 bounded POSIX-like 进程/I/O 子集的用户态消费层，不扩大为完整 POSIX 进程、shell、terminal、权限或 libc 兼容声明。

## Impact

- Affected subsystems: `user` 顶层用户态 libc、crt0 消费边界、packaged `/bin/*` 小程序、用户态 smoke/验证程序、用户态公共头文件、与用户 ABI 共享的 errno/syscall 常量，以及 docs/OpenSpec 中的用户态运行时说明。
- Boot/kernel assumptions: 当前交付目标仍是 x86_64 Legacy BIOS/MBR/exFAT 默认路径；本 change 不改变 boot handoff、ELF 装载地址、linker 地址、页表布局、interrupt/syscall vector、磁盘布局、CR3 切换或内核 syscall ABI。
- Memory assumptions: 继续使用现有用户地址空间、`brk`、restricted anonymous mapping、demand paging/COW 和静态 ELF64 程序模型；不要求 broad file-backed `mmap`、动态 loader、共享库映射、TLS 或 hosted runtime。
- Toolchain/emulator assumptions: 用户程序继续用 x86_64 cross toolchain、freestanding headers、`-nostdlib` 和静态链接构建；运行时验证依赖当前 QEMU/Bochs 与镜像配置，环境不可用时必须记录跳过原因、替代检查和残余风险。
- Non-goals: 不实现动态链接、共享库、完整 POSIX libc、完整 hosted libc、locale、线程、宽字符、浮点格式化、完整 `FILE` 流、完整 shell/job control、完整 terminal process group、SMP、async I/O、UEFI runtime parity 或新 ISA/backend。
