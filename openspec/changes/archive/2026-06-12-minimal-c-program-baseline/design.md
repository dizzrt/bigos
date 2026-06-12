## Context

此前已经建立 freestanding 用户态 runtime、最小 libc、`/bin/sh`、`execve` 和小型 `/bin/*` 打包路径；交互控制台可用性已经把默认文本控制台整理为有界交互入口。简单 C 程序基线不再重新设计用户态运行时，而是在现有能力上把“简单静态链接 C 程序”提升为一等兼容目标：程序应能以稳定入口启动，接收 `argc`/`argv`/`envp`，使用 libc wrapper 与基础输出，获得一致错误报告，并通过 shell 与镜像打包路径被用户观察。

约束保持不变：当前 runtime backend 仍是 x86_64 Legacy BIOS/MBR/exFAT；用户程序是 freestanding C、`-nostdlib -static`、ELF64 `ET_EXEC`、体积有界；用户态只经 `int 0x80` 与内核交互；不改变 syscall vector、寄存器 ABI、页表布局、CR3 切换、boot/storage 布局或内核 ELF 装载边界。

## Goals / Non-Goals

**Goals:**

- 稳定 crt0 到 `main(argc, argv, envp)` 的入口契约和 `main` 返回后的退出路径。
- 稳定用户 libc wrapper 的 `errno` 翻译、失败返回、stdout/stderr 输出和最小 helper 行为。
- 把一组小型静态 C 程序作为 简单 C 程序基线产物，用来覆盖参数、环境、I/O、退出码和错误路径。
- 让 `/bin/sh` 能可靠执行这些程序并展示其输出、错误与退出行为。
- 以 runtime-observable 行为断言保护该基线，而不是依赖源码字符串契约。

**Non-Goals:**

- 不引入动态链接、共享库、hosted runtime、完整 POSIX libc、完整 `stdio`/`FILE`、locale、线程或 C++ 用户程序。
- 不扩展完整 POSIX shell 语义、作业控制、terminal process group、termios、脚本控制流或复杂管道。
- 不新增 kernel syscall 语义、不改现有 syscall 号位、不改 `int 0x80` ABI、不改变 boot/磁盘/页表/链接地址布局。
- 不引入 SMP、异步 I/O、持久完整可写文件系统、广泛设备支持、UEFI 或第二架构 runtime parity。

## Decisions

### 决策 1：简单 C 程序基线只收敛用户态 C 程序契约，不新增内核 ABI

简单 C 程序基线使用 此前已落地的 `execve`、crt0、libc wrapper、fd/VFS、pipe、dup、可写 `/rw` 和 shell 能力。实现中如果发现内核行为缺口，应优先归类为现有能力缺陷修复，而不是新增专用 ABI。

替代方案：为简单 C 程序基线追加新的 syscall 或改动入口布局。否决：这会破坏“简单 C 程序基线”作为兼容性收敛阶段的定位，并扩大对后续 POSIX-like 阶段的耦合。

### 决策 2：以小型 `/bin/*` 程序定义兼容面

简单 C 程序基线的 C 程序集合应覆盖最小但真实的使用面：打印参数与环境、返回指定退出码、触发并报告失败 wrapper、执行基础文件描述符 I/O、输出到 stdout/stderr，并提供一两个可由 shell 组合的工具。每个程序保持单一目的、容量有界、无需动态链接或复杂 libc。

替代方案：只用一个大型综合 smoke 程序。否决：单体程序更难作为用户可见工具复用，也不能验证 shell 对多个外部命令、参数和退出码的基本处理。

### 决策 3：保留用户 libc 的最小边界，但明确失败约定

libc wrapper 对内核负 errno 做统一翻译：面向 C 程序返回 `-1` 或约定失败哨兵，并设置 `errno`。基础输出 helper 必须能把错误说明写到 stderr，但不要求完整 `perror`、`strerror` 表或 hosted stdio。

替代方案：让用户程序直接处理 syscall 返回的负 errno。否决：这会把内核 ABI 泄漏到普通 C 程序，削弱 简单 C 程序基线的兼容目标。

### 决策 4：验证以行为断言为主

简单 C 程序基线验证应从运行时可观察行为出发：程序输出、stderr 错误、退出码、shell 执行结果、管道/重定向组合和镜像中打包路径。源码契约测试可保留用于 syscall/errno 镜像头漂移，但不作为主要兼容证明。

替代方案：继续以源码字符串检查证明 wrapper 或工具存在。否决：该方式无法证明程序能被 ELF 装载、接收参数、运行 wrapper 或经 shell 组合。

## Risks / Trade-offs

- [用户程序集合过大导致镜像或 ELF 体积超界] -> 保持工具单一目的，复用现有 libc，构建超限必须确定性失败并报告产物与体积。
- [crt0/execve 栈布局与程序期望漂移] -> 用参数/环境探针程序和 runtime smoke 断言 `argc`、`argv`、`envp` 内容，而不是只检查源码。
- [libc wrapper 错误约定不一致] -> 为失败路径提供专门小程序，断言 `errno`、返回值和 stderr 报告一致。
- [shell 把程序执行失败误判为 shell 崩溃] -> shell 需求只要求展示外部程序输出、错误和退出行为；shell 自身必须回到读行循环。
- [自动化交互输入不稳定] -> headless 验证使用确定性输入或非交互 smoke；图形 QEMU/Bochs 仅作为可用时的人工补充并记录残留风险。

## Migration Plan

1. 先核查现有 crt0/libc/user-program-build 是否已经满足简单 C 程序基线入口、错误和输出契约，记录缺口。
2. 增补或整理小型 C 程序集合，确保每个程序有稳定名称、用途、退出码和输出行为。
3. 将程序纳入现有用户程序构建与镜像打包路径，保持 `-nostdlib -static`、ELF64 `ET_EXEC` 和体积边界。
4. 通过 shell 与非交互 smoke 运行这些程序，验证参数、环境、stdout/stderr、错误返回、退出码和简单组合。
5. 更新文档，把简单 C 程序基线的 C 程序基线描述为有界兼容目标，而非完整 POSIX 或 hosted C 环境。

回滚策略：该 change 不新增内核 ABI；若某个工具或验证路径不稳定，可从打包集合和 简单 C 程序基线 smoke 中移除该工具，保留已稳定的 crt0/libc/shell 基线。

## Open Questions

- 小型程序集合的最终名称可以在实施时按现有 `user` 目录命名收敛，但必须覆盖参数、环境、输出、错误和退出码五类行为。
- 是否把简单 C 程序基线专用 smoke 复用现有 `userland_smoke`，还是新增更细的开关，取决于现有验证矩阵的维护成本；两种方式都必须保持默认关闭。
