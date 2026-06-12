## Context

BigOS 当前已经具备有界用户态、静态用户 ELF 装载、`int 0x80` syscall ABI、fd/VFS I/O、`brk`、`execve` 参数/环境传递，以及一组最小用户态 libc 实现。Stage 22 的重点不是把 BigOS 提升为完整 hosted C 环境，而是把现有 freestanding 用户态支持收敛为可说明、可依赖、可验证的最小 C 标准库子集。

受影响子系统是 freestanding userland libc 与简单 C 用户程序。该 change 不改变 boot 地址、链接地址、页表布局、IDT/syscall vector、磁盘布局或内核 ABI；默认运行假设仍是 x86_64 Legacy BIOS/MBR/exFAT、单核、有界用户态、静态链接和现有用户 ELF 装载路径。

## Goals / Non-Goals

**Goals:**

- 为最小 C 标准库子集定义明确接口边界，覆盖 syscall wrapper、errno、字符串/内存函数、最小 stdio、堆分配、环境读取和用户态头文件。
- 让简单静态 C 程序可以在不理解内核负 errno、初始栈细节或底层 syscall ABI 的情况下使用稳定 libc 接口。
- 保持 libc 行为与当前内核 syscall、fd 0/1/2、`brk`/受限匿名映射、`execve` 参数/环境和控制台输出路径一致。
- 为后续 POSIX-like 进程/I/O、运行时文件系统和行为导向验证提供统一用户态兼容基线。

**Non-Goals:**

- 不实现 locale、线程、TLS、完整 hosted `stdio`、完整 `FILE` 缓冲流、动态加载器、共享库或完整 POSIX libc。
- 不引入 SMP、完整 job control、terminal process group、session、完整权限模型、广泛 file-backed `mmap`、async I/O 或新的存储/设备 backend。
- 不改变 x86_64 `int 0x80` syscall ABI、用户 ELF 装载 ABI、链接脚本地址、boot/disk layout、IDT vector 或 CR3/page-table 假设。
- 不声明广泛 C/POSIX 标准兼容，只声明 BigOS 当前有界用户态可观察行为下的最小子集。

## Decisions

- 决策：以现有 `user-libc-min` 能力作为唯一规格承载点。
  备选方案是新增 `minimal-c-library-subset` capability，但这会把同一 libc 契约拆成两个规格并增加归档后的重复维护成本。选择修改 `user-libc-min`，因为 Stage 22 是对已有最小用户态 libc 的边界收敛与扩展。

- 决策：保持 wrapper 薄封装，错误语义统一为 POSIX 风格 `errno`。
  备选方案是让 C 程序直接处理内核负 errno，但这会暴露内核 ABI 并削弱 libc 的兼容层价值。wrapper 继续把内核负 errno 翻译为用户态正 errno 与 `-1`/失败哨兵，使小型 C 程序的错误处理稳定。

- 决策：stdio 覆盖 fd 0/1/2 上的薄封装、最小格式化输出，以及 `fprintf(stderr, ...)`。
  备选方案是只要求错误输出通过 `write(2, ...)` 或自定义 helper 完成，但这会让简单 C 程序的错误报告偏离常见 C 习惯。当前阶段引入最小 `FILE`/standard stream 表示，仅支持 `stdin`、`stdout`、`stderr` 上的有限 stdio helper；不引入 `fopen`、完整 buffering、locale、浮点格式化或完整 hosted `FILE` 语义。

- 决策：堆分配保持有界、确定性、可失败。
  备选方案是引入更复杂的 coalescing、arena 或 mmap-backed allocator，但当前内存/文件模型仍有界。`malloc` 失败返回 NULL 且不破坏既有块，`free(NULL)` 无副作用，分配器不承诺线程安全或 hosted allocator 行为。

- 决策：在本阶段引入更细粒度的标准头文件名，同时保留 BigOS umbrella 头。
  备选方案是继续只以单一 BigOS 最小公共头承载所有 C 程序接口，但这会让简单 C 程序难以按常见 C include 习惯组织依赖。当前阶段提供有限的 `stdio.h`、`stdlib.h`、`string.h`、`errno.h`、`unistd.h`、`fcntl.h`、`sys/types.h`、`sys/wait.h` 等 freestanding-safe 头文件名；每个头只声明 BigOS 已实现的最小子集，umbrella 头继续作为兼容入口。

- 决策：验证以构建检查和专门的 libc subset runtime smoke 为主。
  备选方案是复用现有 userland smoke 承载所有行为断言，但这会把 libc 子集回归与 shell/init 综合行为耦合。当前阶段应拆出专门的 libc subset smoke 覆盖头文件、stdio、`fprintf(stderr, ...)`、errno、环境、堆和字符串/内存函数；现有 userland smoke 继续作为组合路径回归。

## Risks / Trade-offs

- [Risk] 最小 libc 接口名看起来像 hosted libc，调用者可能误以为完整标准兼容。→ Mitigation: 在规格、公共头注释和文档中明确 bounded subset 与 non-goals。
- [Risk] 用户态 errno mirror 与内核 errno 数值漂移。→ Mitigation: 保留单一来源同步检查，并要求 wrapper/头文件验证覆盖常用 errno。
- [Risk] `printf`、`fprintf` 或 malloc 的边界行为被过度依赖。→ Mitigation: 只规格化已支持格式、standard streams、失败返回和有界分配语义，不承诺完整格式化、文件流、缓冲、locale 或线程安全。
- [Risk] 运行时行为验证依赖本地 QEMU/Bochs、交叉工具链和磁盘镜像环境。→ Mitigation: 将源级/构建检查与 emulator smoke 分层记录，环境不可用时明确跳过原因和残余风险。
- [Risk] 扩展用户态头文件时误引入宿主 libc 假设。→ Mitigation: 所有 libc 头文件保持 freestanding-safe，仅声明 BigOS 实现的接口、类型和常量。

## Migration Plan

1. 盘点现有用户态 libc 函数、头文件和小型 C 程序使用点，确认它们落在最小子集范围内。
2. 补齐或收敛细粒度标准头文件声明、umbrella 头、errno mirror、wrapper 返回约定、stdio 边界、环境读取和有界堆行为。
3. 更新或新增专门的 libc subset smoke，使输出、`fprintf(stderr, ...)`、错误、参数/环境、堆和 fd 行为可观察。
4. 运行最窄可用构建与 OpenSpec 校验；具备环境时运行分层 emulator 行为检查。
5. 如实现阶段发现某接口会暗示完整 hosted/POSIX 行为，优先缩小接口或记录为 non-goal，而不是扩大本阶段范围。

Rollback strategy: 本 change 主要收敛用户态 libc 与规格边界；若实现引入回归，可回退新增 libc 接口或用户程序用例，保留现有 syscall ABI、crt0、ELF 装载和内核子系统行为不变。

## Resolved Decisions

- 增加最小 `fprintf(stderr, ...)`，但仅承诺 standard streams 上的有限格式化输出，不引入完整 hosted `FILE` 或文件流语义。
- 引入更细粒度的标准头文件名，保持 freestanding-safe 且只暴露 BigOS 已实现的最小子集；保留 umbrella 头作为兼容入口。
- 行为断言默认拆分为专门的 libc subset smoke，现有 userland smoke 继续覆盖 init/shell/用户程序组合路径。
