## Context

BigOS 当前已经从早期 syscall 入口推进到默认用户态 baseline：内核通过 `int 0x80` 提供进程、fd/VFS、pipe、time/identity、signal、`brk`、受限匿名映射和 `execve` 等能力，用户侧已有 crt0、最小 libc、`/bin/sh` 与若干静态用户程序。stage 33 的核心问题不是新增用户态功能，而是在现有 x86_64-only baseline 上把 kernel/user ABI 事实收束到可审查、可文档化、可复用的边界。

本设计涉及 `kernel/core/syscall`、`kernel/core/proc`、用户态 runtime/libc/build、公共头文件和双语文档。它必须保持当前 runnable backend、内存布局、IDT vector、syscall 寄存器约定、用户栈布局和磁盘镜像布局不变。

## Goals / Non-Goals

**Goals:**

- 建立 syscall ABI 的单一来源，覆盖 syscall number、参数寄存器、返回寄存器、负 errno、用户态 errno 翻译和 wrapper 约定。
- 让用户态公共头文件只暴露当前已实现且有 OpenSpec 约束的 ABI、常量、类型和函数声明。
- 使 crt0、libc wrapper、用户程序构建和内核 syscall dispatcher 的契约在规格、源码和文档中保持一致。
- 将 x86_64 私有寄存器、IDT/TSS、汇编 frame 和 ring3 entry 细节限制在 architecture/backend 实现侧，核心层只消费稳定语义。
- 为后续 backend 扩展前的 ABI 清理提供 targeted validation 路径。

**Non-Goals:**

- 不新增第二 backend、UEFI backend、non-x86 ISA backend 或 runtime parity 层。
- 不把 `int 0x80` 替换为 `syscall/sysret`，不改变 syscall vector、寄存器顺序或返回语义。
- 不改变 linker 地址、CR3 切换语义、用户栈布局、ELF 装载限制、磁盘镜像布局或 boot handoff。
- 不实现完整 POSIX libc、动态链接、共享库、线程 runtime、job control、SMP、持久完整可写文件系统或广泛 file-backed `mmap`。

## Decisions

1. 以现有 `int 0x80` ABI 作为当前唯一稳定 syscall ABI。

   理由：现有内核、用户 libc 和 smoke 已经围绕 `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` 建立最小契约，stage 33 的目标是收紧边界而不是替换入口机制。替代方案是引入 `syscall/sysret` 或 backend-neutral syscall trap 抽象；这会把 runtime 行为变更混入边界整理，并提高 boot/user-mode 风险。

2. 让 kernel/user ABI 事实从单一来源传播到用户 wrapper 与文档。

   理由：syscall number、errno 数值、wrapper 失败哨兵和用户态头文件如果重复定义，后续 backend 或用户程序扩展会更容易产生漂移。实现时应优先复用已有公共 ABI 头，必要时补充静态检查或一致性检查。替代方案是在用户态复制一套编号和 errno；这会降低 freestanding 构建耦合，但会制造长期 ABI 漂移风险。

3. 用户态公共头文件采用“已实现且有规格约束才暴露”的策略。

   理由：BigOS 仍是 bounded POSIX-like subset，头文件如果声明未实现接口，会让用户程序和文档误以为完整 POSIX 或 hosted libc 可用。替代方案是提供宽泛兼容头；这对移植示例程序更方便，但会破坏当前成熟度表述。

4. 核心层只表达 syscall/user ABI 的语义边界，x86_64 私有细节留在 backend 实现侧。

   理由：当前唯一 runnable backend 仍是 x86_64，但后续 stage 会继续整理中断、VM、用户态入口和 backend 扩展。把裸寄存器布局、descriptor、TSS/RSP0、IDT gate 或汇编 frame 泄漏到核心层会增加后续拆分成本。替代方案是保持当前散落实现；短期改动少，但会让 stage 34/35 的边界整理更难审查。

5. ABI 文档合并进现有 syscall/userland 文档，不新增独立 `user-abi.md`。

   理由：当前 `docs/en/arch/syscall-entry.md` 已承载 vector、register、errno、dispatcher 等 syscall ABI 事实，`docs/en/arch/userland-runtime.md` 已承载 crt0、libc、用户程序构建和 bounded userland runtime 事实；`docs/zh` 下也存在同构镜像。stage 33 应先收紧既有文档中的合同，避免新增空泛的第三份 ABI 文档造成漂移。替代方案是新增 `docs/en/arch/user-abi.md` 和 `docs/zh/arch/user-abi.md`；只有当 syscall 与 userland runtime 文档明显膨胀，或后续 backend parity 需要单独 ABI 索引时再考虑。

6. 先补齐已使用的 BigOS-specific 用户头文件规格，再决定是否隐藏声明。

   理由：初步排查显示用户程序主要通过 `libc.h` 和细粒度 libc 头消费接口，没有直接包含 kernel-private 实现头；但 `strchr`、`wait_status`、`bigos_readdir`、raw `syscall0`-`syscall6`、`brk_raw`、`mmap_anon`、`time_now`、`get_tick` 等声明已经存在或被内部/示例程序使用，其中部分只有泛化规格覆盖。实现阶段应先补齐这些声明的显式 bounded 规格与头文件归属，再按“有实现且有规格才暴露”的策略保留或隐藏。替代方案是直接隐藏可疑声明；这可能破坏现有 shell、smoke 或 libc 内部路径，且会把兼容性决策和规格补齐混在一起。

7. syscall wrapper 的寄存器与 clobber 约束写成源码级静态检查。

   理由：用户态 raw syscall primitive 是当前 ABI 最敏感的代码点，`r10` 第四参数、`r8`/`r9` 后续参数、`rax` 返回、以及 `rcx`/`r11`/`memory` clobber 不应只靠文档和 smoke 兜底。实现阶段应新增源码级 contract 检查，直接检查 wrapper 汇编约束和 clobber 列表，并继续保留 runtime smoke 作为行为验证。替代方案是只依赖文档和 QEMU smoke；这能观察部分行为，但很难覆盖 register constraint 漂移。

8. 验证按变更触及范围分层。

   理由：纯规格、文档和 consistency 整理不需要强制 QEMU/Bochs；一旦修改 syscall dispatcher、用户 wrapper、crt0、用户栈或公共头文件，需要至少执行最窄构建检查，并在环境支持时运行对应 headless QEMU smoke。替代方案是每次都跑完整 emulator matrix；安全但成本高，且不适合文档-only 变更。

## Risks / Trade-offs

- [Risk] 将 ABI 单一来源迁移过度可能触发 kernel/user include 循环或 freestanding 头文件污染 -> Mitigation: 公共 ABI 头保持小而自包含，只放 number、errno、基础类型和 wrapper 所需声明。
- [Risk] 清理 syscall wrapper 时改变寄存器约定或 clobber 假设 -> Mitigation: 保留现有寄存器顺序，补充源码级检查或最小用户程序验证，避免引入新汇编入口。
- [Risk] 头文件收敛可能让未受支持但已有示例偶然包含的声明消失 -> Mitigation: 只移除未实现或无规格约束的声明，并在 tasks 中要求 targeted search 和文档更新。
- [Risk] 文档把 bounded subset 描述成完整 POSIX 或跨架构支持 -> Mitigation: `docs/en` 与 `docs/zh` 同步使用 bounded wording，明确当前唯一 runnable backend 是 x86_64 Legacy BIOS。
- [Risk] 边界抽象过早，导致低层硬件事实被隐藏 -> Mitigation: 核心接口描述语义，架构实现仍显式保留硬件常量和 ABI 断言，不抽象 boot/linker/IDT/memory layout。

## Migration Plan

1. 盘点当前 syscall number、dispatcher、用户 wrapper、errno、crt0 退出路径、用户程序构建头文件依赖和双语文档中的 ABI 事实。
2. 确定或补齐单一 ABI 来源，并让 kernel dispatcher 与 user wrapper 从该来源消费可共享的 number、errno 和最小类型定义。
3. 补齐已使用 BigOS-specific 用户态声明的显式规格，尤其是 `strchr`、`wait_status`、`bigos_readdir`、raw syscall primitive、`brk_raw`、`mmap_anon`、`time_now` 和 `get_tick` 的 bounded 归属。
4. 收敛用户态公共头文件，移除或隐藏未实现、无规格约束或会暗示 hosted/POSIX 完整支持的声明。
5. 更新既有 `docs/en/arch/syscall-entry.md`、`docs/en/arch/userland-runtime.md` 及对应 `docs/zh` 镜像，保持技术事实一致并明确非目标；本阶段不新增独立 `user-abi.md`。
6. 为 userland syscall primitive 增加源码级静态 contract 检查，覆盖寄存器绑定、参数顺序和 clobber 列表。
7. 执行 OpenSpec 状态检查、targeted consistency search；若触及源码行为，执行最窄构建检查和可用的 userland/syscall smoke。

Rollback 策略：若源码整理引入构建或 smoke 失败，先回退到当前 dispatcher/wrapper include 关系和头文件暴露集合，保留已确认无争议的文档 bounded wording；不得通过改变 syscall ABI、boot 地址或用户栈布局来规避失败。

## Open Questions

- 无。当前阶段按上述决策推进；若后续实现排查发现新的已使用但无规格约束声明，先补充对应规格，再决定是否继续暴露。
