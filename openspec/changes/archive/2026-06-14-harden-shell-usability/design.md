## Context

BigOS 当前默认启动 resident init 并进入 `/bin/sh`，用户态已有 bounded libc、PATH 查找、cwd、路径工具、pipe、dup/dup2、重定向、wait/exit 和运行时 smoke 基线。Stage 32 聚焦把这些已经存在的能力组合成更稳定的交互式 shell 使用体验，而不是新增完整 POSIX shell。

受影响子系统主要位于用户态 shell 与用户程序路径，并通过现有 syscall/fd/VFS/process 契约消费内核能力。该设计不改变 x86_64 Legacy BIOS/MBR/exFAT 启动路径、内核链接地址、IDT/syscall vector、页表布局、磁盘布局或 CR3 切换规则。

## Goals / Non-Goals

**Goals:**

- 让 shell 对路径、解析、命令查找、重定向、pipe、exec 和 wait 失败给出确定性错误，并继续下一轮 read-parse-execute。
- 让 shell 维护最近一次内建命令、外部命令、pipe 组合或重定向失败的有界退出状态，供后续行为和验证路径观察。
- 保证重定向和 pipe 建立失败不会污染父 shell 的 stdin/stdout/stderr 或无关 fd。
- 让 packaged path tools 和简单 C 程序在 PATH、cwd-relative path、pipe 和重定向组合下保持可观察 stdout/stderr 与 errno/exit-status 行为。
- 增加行为导向验证覆盖，并在 emulator/toolchain 不可用时记录 skipped validation 与剩余风险。

**Non-Goals:**

- 不实现 job control、background jobs、terminal process groups、sessions、termios 或完整 terminal control。
- 不实现完整 POSIX shell grammar、quoting、escaping、globbing、variable expansion、command substitution、脚本语言或多级 pipeline。
- 不引入动态链接、hosted libc、完整 POSIX utility suite、持久完整可写文件系统、async I/O、SMP、UEFI 或第二 architecture backend。
- 不改变 syscall ABI、用户可见寄存器约定、boot image layout、filesystem image discovery 或硬件驱动行为。

## Decisions

1. Shell 可用性硬化留在用户态 shell 主控制流中。

   Rationale: 解析、错误展示、PATH 尝试、组合命令退出状态和 prompt 恢复都是 shell 用户可见策略，应避免把 shell 语言策略下沉进内核。内核继续只提供 bounded process、fd/VFS、pipe/dup、cwd 和 errno 契约。

   Alternative considered: 在内核 syscall 或 init 中加入 shell-specific 状态跟踪。该方案会把用户态策略耦合到内核 ABI，增加未来 ABI 边界清理成本，因此不采用。

2. 重定向和 pipe 使用两阶段 setup，并保持父 shell fd 快照/恢复语义。

   Rationale: pipe 或 open/dup2 失败时最容易破坏交互 shell 的标准 fd。实现应在执行目标命令前完成 bounded setup；失败路径关闭已创建但未交付的 fd，并恢复父 shell 的标准 fd 状态。外部命令成功 setup 后仍通过现有 fork/execve/wait 路径运行。

   Alternative considered: 直接在父 shell 上原地改 fd，依赖失败分支逐个修复。该方式更容易遗漏异常路径，且难以验证父 shell 后续命令仍可用。

3. 退出状态采用有界 shell-local 结果模型，不新增 POSIX `$?` 或变量机制。

   Rationale: BigOS 目前不需要完整 POSIX `$?` 或 shell variable 语义，但 validation 和 shell 内部决策需要观察最近命令成功/失败。shell 应将内建、命令查找失败、exec 失败、重定向 setup 失败和 pipe 组合结果折叠为 bounded status，并以验证可观察方式消费；如实现阶段确实需要交互式观察，可以增加 BigOS-specific `status` 内建命令，只打印最近 bounded status，不引入变量展开或脚本语义。

   Alternative considered: 暂不保存退出状态，只依赖 stdout/stderr 文本。该方式无法稳定验证失败组合，也会让简单脚本化 smoke 难以判断行为。

4. 组合范围保持单级 pipe 与基本重定向。

   Rationale: roadmap 明确要求改进 pipe、redirection 和小工具组合体验，但保持无完整 POSIX shell 语言边界。单级 pipe、`<` 和 `>` 足以覆盖当前 path tools 与简单 C 程序的主要组合路径。pipeline status 采用 POSIX-like 末端命令规则：setup 失败时记录 shell setup failure；两个子进程均启动后，shell 等待两端完成，但 pipeline 的 bounded status 来自末端命令。

   Alternative considered: 顺便引入多级 pipeline、append、stderr 重定向或 quoting。该方案会扩大解析器和 fd 状态机复杂度，偏离 stage 32 的 hardening 目标。

5. 验证优先覆盖用户可见行为，再根据环境选择 emulator runtime。

   Rationale: shell usability 是 kernel-to-userland 能力闭环，应验证内建、外部工具、cwd-relative path、redirection、pipe、错误恢复和 exit status。优先扩展现有 `userland_smoke` 覆盖 shell usability，不新增独立 shell usability smoke 开关，以避免 smoke 矩阵碎片化；QEMU headless serial/output 是首选自动化路径，缺少 `x86_64-elf-*`、xmake、QEMU/Bochs、ROM/display 或 disk image 配置时记录跳过与替代检查。

   Alternative considered: 新增独立 shell usability smoke 开关或只做源码字符串检查。独立开关会增加 smoke 选择复杂度且与现有 userland 端到端验证重叠；源码字符串检查不能证明 shell 组合路径在真实 boot/userland 环境中可观察。

## Risks / Trade-offs

- [Risk] shell 解析器硬化时引入超出 bounded grammar 的隐式支持 -> Mitigation: 明确只接受现有空白分割、单级 pipe 和基本 `<`/`>`，不加入 quoting/globbing/variables。
- [Risk] fd 恢复路径遗漏导致交互 shell 后续不可用 -> Mitigation: 将父 shell 标准 fd 保存/恢复、未发布 fd 关闭和失败后继续执行作为规格与任务中的显式检查。
- [Risk] pipe 上游失败可能被末端命令成功状态掩盖 -> Mitigation: 明确采用 POSIX-like 末端命令规则，同时让验证覆盖上游失败的 stderr/stdout 可观察性，不把该行为扩展为完整 POSIX shell。
- [Risk] runtime validation 依赖本地 emulator/toolchain，可能不可复现 -> Mitigation: validation artifact 必须记录 executed/skipped/blocked、缺失依赖、替代检查和剩余风险。
- [Risk] 小工具组合暴露既有 libc 或 VFS errno 缺口 -> Mitigation: 保持错误为 deterministic errno-based 或 shell deterministic error，不在本 change 中扩大 filesystem 或 libc 范围。

## Migration Plan

1. 更新 OpenSpec delta，明确 shell hardening 的 bounded 行为和非目标。
2. 审视现有 shell、libc wrapper、path tools 和 userland smoke 的实现，先补齐失败路径和状态传播，再补充行为验证。
3. 通过源级检查确认不改变 boot、syscall ABI、disk layout 和硬件路径。
4. 在环境可用时通过现有 `userland_smoke` 覆盖最窄的 shell usability runtime 行为；环境不可用时记录跳过、替代检查和剩余风险。
5. 若实现后出现交互 shell 回归，可回滚到原 shell 执行路径，因为本设计不要求内核 ABI 或磁盘格式迁移。

## Resolved Questions

- 最近命令状态保持为 shell-local bounded status，供验证路径和 shell 内部决策使用；不引入 POSIX `$?`、变量展开或脚本语义，只有在实现确有需要时才增加 BigOS-specific `status` 内建命令。
- pipe 组合状态采用 POSIX-like 末端命令规则：setup 失败记录 shell setup failure；两个子进程均启动后，pipeline bounded status 来自末端命令，同时等待并回收两端子进程。
- shell usability runtime 验证扩展现有 `userland_smoke` 覆盖，不新增独立 shell usability smoke 开关。
