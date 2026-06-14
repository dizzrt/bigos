## Why

当前 BigOS 已经具备默认 PID-1 init、`/bin/sh`、最小 libc、用户程序构建与多组用户态 syscall 能力；在继续推进更多 backend 或更复杂用户态能力前，需要把 syscall ABI、用户可见寄存器约定、用户态公共头文件和 kernel/user 契约收紧为可审查边界，避免 x86_64 细节继续扩散到用户态和核心层。

本变更用于在现有 x86_64-only baseline 上稳定 kernel/user ABI 事实、文档和源码入口，不用于新增第二 runtime backend，也不用于扩大 BigOS 的 POSIX 兼容承诺。

## What Changes

- 明确 syscall number、参数寄存器、返回寄存器、错误返回、用户态 wrapper 与 kernel dispatcher 之间的单一 ABI 契约。
- 收敛用户态公共头文件边界，确保只暴露当前已实现且有规格约束的最小 libc/syscall 接口。
- 对齐 crt0、用户程序构建、libc wrapper 与内核 syscall 表之间的契约描述，避免用户程序直接依赖未文档化的内核或 x86_64 私有细节。
- 补充 kernel/core 与 architecture 边界要求，要求 syscall/user ABI 相关核心消费点通过稳定语义接口表达，而不是散落裸寄存器布局或汇编入口假设。
- 保持现有 `int 0x80`、x86_64 Legacy BIOS/MBR/exFAT、单核执行、现有内存布局和用户态最小子集不变。
- 非目标：不新增 UEFI 或第二 ISA backend，不引入 `syscall/sysret` 指令路径，不实现动态链接、完整 POSIX libc、完整 job control、SMP 或广泛 file-backed `mmap`。

## Capabilities

### New Capabilities

- 无。本变更整理并收紧现有能力的边界，不新增独立能力族。

### Modified Capabilities

- `syscall-entry`: 收紧 `int 0x80` syscall ABI 的单一来源、寄存器约定、错误返回和用户态可见契约。
- `user-libc-min`: 收紧最小 libc wrapper、errno 翻译和用户态公共头文件暴露边界。
- `user-crt0-runtime`: 明确 crt0 只消费稳定用户入口与 syscall 退出契约，不依赖内核私有栈/寄存器细节之外的未文档化行为。
- `user-program-build`: 明确用户程序构建产物只依赖 freestanding 用户态 ABI 头文件和静态链接约束。
- `architecture-core-boundary-discipline`: 补充 syscall/user ABI 边界整理时核心层不得扩散 x86_64 私有实现细节的要求。

## Impact

- 受影响子系统：`kernel/core/syscall`、`kernel/core/proc` 中用户态进入和 exec 相关契约、`user` runtime/libc/build 入口、`include` 中用户可见 ABI 头文件，以及 `docs/en` 与 `docs/zh` 中的 syscall/userland 文档。
- ABI 影响：现有 syscall vector、寄存器约定、负 errno 返回、用户态 errno 翻译和 `SYS_EXIT`/`SYS_WRITE` 等已暴露接口语义必须保持稳定；如发现未文档化或重复定义的 ABI 事实，应迁移到单一来源并补齐文档。
- 架构假设：当前唯一 runnable backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；保留现有 linker 地址、IDT vector、TSS/RSP0、CR3 切换、用户栈布局和磁盘镜像假设，除非后续独立 change 明确改变。
- 工具链与验证假设：继续使用 xmake 与 `x86_64-elf-gcc` 交叉工具链；涉及源码行为的整理需要执行最窄构建或 smoke，文档/规格整理至少执行 OpenSpec 状态检查和 targeted consistency search。
