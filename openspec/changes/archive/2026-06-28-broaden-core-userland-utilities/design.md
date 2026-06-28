## Context

BigOS 当前默认启动路径已经进入 resident PID-1 init，并通过 `/bin/sh` 消费打包在 `/bin` 下的静态用户程序。`user/bin` 已存在一批基础工具，`tools/bigosdev/core.py` 维护默认 `/bin` 打包清单，`user/bin/tool_common.h` 提供小型共享 helper，`user/smoke/userland_smoke.c` 已覆盖部分 shell、fd、cwd、目录和工具组合行为。

本变更的关键不是引入新的内核架构，而是把核心用户态工具集合收敛成一组可维护、可验证的 bounded utilities。设计必须保持 x86_64 Legacy BIOS/MBR/exFAT 默认镜像布局、ELF64 用户程序加载、`int 0x80` syscall ABI、用户地址空间布局和现有 `/rw` 运行期文件系统语义不变。实现应优先修复或补齐工具层消费路径；只有发现既有 wrapper 或 syscall 明显不满足已规格化行为时，才进行最小内核/用户 libc 修正。

## Goals / Non-Goals

**Goals:**

- 建立有界核心工具分类：文件字节流、目录/路径、元数据、文本过滤、时间/进程辅助、shell 组合和必要的 BigOS 专用维护工具。
- 保证工具通过现有 freestanding user libc、静态链接、`/bin` 打包、`fork`/`execve`/`wait`、fd/VFS、cwd、pipe/redirection 和 shell PATH 路径工作。
- 统一工具的错误输出、退出状态、资源上限和 unsupported option 行为，使验证可以从输出、退出码或串口日志做确定判断。
- 扩展行为验证，覆盖代表性工具在 `/boot` 只读文件、`/rw` 可写目录、相对路径、pipe/redirection、文本处理、失败路径和 shell 组合中的行为。
- 同步文档，说明这是 BigOS bounded userland utilities，而不是完整 POSIX/GNU coreutils。

**Non-Goals:**

- 不实现完整 coreutils、完整 POSIX utility set、完整 shell grammar、job control、后台任务、glob、变量展开、脚本控制流或终端进程组完整语义。
- 不新增动态 loader 依赖、hosted libc 依赖、线程、locale、宽字符、Unicode 文本处理、完整正则、完整排序、权限模型、symlink、link、file lock 或 async I/O。
- 不改变 boot address、linker address、IDT/syscall vector、CR3 切换、用户 ABI、磁盘分区布局、exFAT 镜像布局或 `/rw` 持久性承诺。
- 不把当前工具行为包装成对外的完整 POSIX 兼容承诺；未实现的选项必须明确失败或保持未声明。

## Decisions

### Decision: 以工具分类定义支持面，而不是承诺逐项 POSIX 兼容

核心工具集合按 BigOS 当前能力分层：`cat`/`tee`/`head`/`tail`/`wc`/`grep`/`hexdump` 等字节与文本过滤，`ls`/`find`/`du`/`stat`/`basename`/`dirname` 等路径观察，`mkdir`/`rm`/`rmdir`/`touch`/`truncate`/`cp`/`mv`/`rename`/`write`/`append` 等运行期文件操作，`date`/`sleep`/`kill` 等时间和进程辅助，以及 `mkfs_bigfs` 等 BigOS 专用维护入口。

理由：BigOS 的文件系统、libc、shell 和 process model 均为有界子集，按场景分类比逐项复刻 POSIX/GNU 选项更稳定，也更容易写出可验证需求。

备选方案是引入一份“coreutils 兼容列表”。该方案容易误导为完整兼容，并会把大量 locale、权限、排序、正则、链接和 shell 语义拉入范围，因此不采用。

### Decision: 保持每个工具为小型 freestanding C 程序

新增或修正工具默认继续放在 `user/bin`，使用现有 user libc public headers 和 `tool_common.h` 中的受限 helper。工具不得依赖宿主头、动态初始化、线程、异常、hosted stdio 扩展或外部库。只有出现真实重复且不适合 header-only 的逻辑时，才考虑在用户态 libc 或构建系统中加入更通用的支持。

理由：当前构建和打包模型已经围绕单个静态用户 ELF 工作，保持该模型可以降低 linker、loader、打包、验证和回滚风险。

备选方案是引入共享用户态工具库。该方案会在动态链接、归档库链接顺序和用户程序体积限制上增加新变量；在本阶段收益不足。

### Decision: 打包清单是默认可见工具集合的单一入口

默认可见工具必须进入用户程序构建产物和镜像 `/bin` 打包清单，并保持单个工具文件大小在现有 user ELF loader bound 内。工具的名字、安装路径和 shell PATH 解析必须对齐，避免源码存在但默认系统不可执行。

理由：BigOS 正常启动的用户体验取决于镜像内是否真的存在 `/bin/*`，只构建不打包无法满足用户可见目标。

备选方案是按需从 `/boot` 或其他目录加载工具。该方案会扩大文件系统布局和 shell 路径语义，不在本变更范围内。

### Decision: 失败路径统一走工具层错误格式和退出码

工具应对打开、读取、写入、枚举、metadata、参数解析和 unsupported option 失败返回非零退出状态，并向 stderr 输出包含工具名、目标路径或操作和 errno 文本的确定性错误。成功路径返回 0；部分成功但出现至少一个输入失败的多文件工具返回非零。

理由：现有串口/输出验证需要稳定文本和退出状态；工具间一致性也能减少 shell 组合中的诊断歧义。

备选方案是模拟各 GNU 工具的细粒度退出码。该方案对 BigOS 当前用户态没有必要，并会引入兼容承诺。

### Decision: 验证以组合场景为主，源码检查为辅

验证应包括静态构建、镜像打包检查、`userland_smoke` 或等价 smoke 中的非交互 shell 脚本，以及针对工具的用户程序直接执行。场景应覆盖 `/boot` 只读读取、`/rw` 创建/修改/删除、相对路径、管道、重定向、文本过滤和失败路径。QEMU headless 是首选运行验证；Bochs 仅在启动或硬件行为相关风险出现时补充。

理由：工具的价值主要在组合路径，单个函数单测不足以覆盖 fd、cwd、exec、shell 和 VFS 的交互。

备选方案是只做构建验证。该方案无法证明镜像打包和 shell 可见执行，风险较高。

## Risks / Trade-offs

- [Risk] 工具数量增加导致用户镜像超过当前打包或 loader bound → Mitigation: 保持单工具小型实现，复用 `tool_common.h`，在打包阶段继续检查单文件大小和镜像容量。
- [Risk] 工具行为不一致，导致 shell 组合和验证输出不稳定 → Mitigation: 统一 stderr 格式、退出状态、参数解析和 unsupported option 处理。
- [Risk] 文本工具被误读为支持完整 regex、locale、Unicode 或排序语义 → Mitigation: specs 和文档明确限定为字节/ASCII/C-locale 风格子集，未支持选项确定性失败。
- [Risk] 文件修改工具暴露 `/rw` 或 exFAT 只读边界缺陷 → Mitigation: 优先依赖既有 runtime filesystem requirements；发现底层违反既有规格时做最小修复并补充验证记录。
- [Risk] 默认打包清单与源码目录漂移 → Mitigation: 将构建产物、打包清单和文档中的工具集合纳入任务检查。

## Migration Plan

1. 盘点现有 `user/bin` 工具与默认打包清单，确定哪些工具已满足规格、哪些需要修正或补齐。
2. 分批实现或修正工具行为，优先覆盖高频路径：文件/目录、文本过滤、shell 组合、时间/进程辅助。
3. 扩展 `userland_smoke` 或等价验证程序，确保代表性工具组合可在 QEMU headless 中观察。
4. 同步英文/中文文档，说明默认 `/bin` 工具集合和有界非目标。
5. 回滚策略是从打包清单移除有问题的新工具或恢复单个工具源码；不涉及磁盘布局、内核 ABI 或 boot path 迁移。

## Resolved Decisions

- 网络诊断类工具不纳入本批默认核心工具，留到 socket/userland 网络体验的后续变更中处理。
- `grep` 本批只规格化字面子串匹配，不加入 pattern 子集或正则语义，避免扩大 regex 兼容承诺。
