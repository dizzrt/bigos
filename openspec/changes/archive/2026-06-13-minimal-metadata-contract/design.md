## Context

BigOS 当前已经有只读 exFAT、RAM-backed `/rw`、fd/VFS、进程 fd table、用户缓冲校验、最小 libc、`/bin/sh` 和打包小程序。文件内容 I/O、目录创建/删除、最小目录枚举和权限边界已有有界语义，但文件与目录元数据仍没有统一的 kernel-to-userland 查询契约。

本设计面向当前 x86_64 Legacy BIOS/MBR/exFAT 运行后端，不改变 boot、磁盘布局、页表布局、syscall 向量、上下文切换或中断路径。元数据查询只在普通可阻塞进程 syscall 上下文运行；IRQ、调度临界区和 preemption-disabled 不可阻塞路径不得执行会分配、等待或同步块 I/O 的查询。

## Goals / Non-Goals

**Goals:**
- 定义一个有界、稳定、跨后端的最小元数据结构，支持简单程序观察文件类型、大小、基础 mode/owner 和后端可稳定提供的最小属性。
- 为路径查询和 fd 查询提供一致的内核契约，并把失败映射为确定性负 errno。
- 通过 freestanding libc 暴露 `stat`/`fstat` 风格 wrapper、公共类型和常量，让用户程序按正 errno 约定处理失败。
- 提供小型用户态消费路径，使 shell 或打包工具能自然展示元数据，而不要求用户直接调用裸 syscall。
- 增加行为导向验证，覆盖只读 exFAT、`/rw`、目录、常规文件、非法 fd、缺失路径和用户缓冲失败。

**Non-Goals:**
- 不实现符号链接、设备节点、硬链接、mount namespace、`chroot`、完整路径规范化或相对路径语义。
- 不实现完整 POSIX `struct stat`、完整权限数据库、ACL、扩展属性、完整时间戳语义、inode 稳定持久编号承诺或广泛标准兼容。
- 不改变当前 x86_64 syscall 入口向量、寄存器约定、GDT/TSS、CR3 切换、页表布局、boot 地址或链接地址。
- 不引入 SMP、异步 I/O、动态链接、共享库、完整 POSIX libc、完整 shell 语言、job control 或 terminal process group。
- 不把 `/rw` 从运行期 RAM-backed 一致性扩展为跨重启持久化文件系统。

## Decisions

### Decision: 使用 BigOS 自有有界元数据结构

采用 BigOS 用户 ABI 专用的固定布局元数据结构，而不是声明完整 POSIX `struct stat` 兼容。结构只包含当前后端能稳定提供且简单程序需要的字段：文件类型、大小、mode、uid、gid、链接计数的有界占位、用户可见对象编号默认零值，以及为未来扩展保留的显式零填充字段。第一版不向用户 ABI 暴露 `/rw` 运行期 inode 编号或 exFAT 后端编号，避免形成持久 inode、跨后端稳定编号或 POSIX `st_ino` 兼容承诺；后续如确有真实消费场景，可通过单独 change 扩展该字段语义。

备选方案是直接复刻 POSIX `struct stat`。该方案会暗示未实现的设备号、完整 inode、时间戳、链接和权限数据库语义，超出 roadmap TTY console input capability7 的边界，因此不采用。

### Decision: VFS 层统一路径和 fd 元数据查询

路径查询由 VFS 解析绝对路径并向具体后端请求 metadata snapshot；fd 查询从当前进程 fd table 取得 open file object 并请求其 metadata snapshot。两条路径最终填充同一内核内部元数据结构，再复制到用户缓冲。目录 fd 的 `fstat` 只覆盖当前已经能以 open file object 表示的目录对象或最小目录枚举消费路径，不为了本阶段额外引入通用 open-directory 语义。

这样可避免 exFAT、`/rw`、管道或未来对象类型直接暴露互不兼容的用户 ABI。备选方案是在每个后端直接实现 syscall 复制逻辑；该方案会重复用户缓冲校验和 errno 映射，增加 ABI 分叉风险。

### Decision: 查询语义保持同步、可阻塞且有界

元数据查询沿用当前 fd/VFS 同步模型，只允许在可阻塞进程上下文执行。路径长度、字段宽度、目录项、文件大小和后端对象数量继续受现有有界限制约束。所有分配、路径解析、后端读元数据和用户缓冲复制失败都返回确定性错误。

备选方案是为元数据引入缓存或异步查询。当前系统没有 async I/O 和 SMP，额外缓存还会增加一致性问题，因此不在本阶段引入。

### Decision: libc 只提供薄 wrapper 和最小头文件暴露

用户态 libc 提供 `stat`/`fstat` 风格 wrapper、类型和常量，按现有 syscall wrapper 约定把内核负 errno 翻译为用户态 `errno` 并返回失败哨兵。公共头只声明 BigOS 明确支持的字段和常量，不声明完整 hosted/POSIX 接口。

备选方案是仅提供裸 syscall。该方案会让简单 C 程序重复 ABI 细节和负 errno 翻译，不符合 bounded libc baseline 的方向。

### Decision: 用户态消费以小工具和可选 shell 展示为主

新增独立的小型 `stat` 风格用户程序展示元数据，shell 仅作为启动和组合入口消费该工具。输出格式保持确定性、适合行为验证，不把本阶段扩展为完整 POSIX `stat`、完整 POSIX `ls -l`、glob 或脚本环境。

备选方案是大幅扩展 shell 内建、复用路径工具输出或引入更复杂格式化能力。该方案会把 TTY console input capability7 扩大为 shell 语言和工具套件建设，也会模糊 metadata contract 的第一版验证入口，因此不采用。

## Risks / Trade-offs

- [Risk] 过度接近 POSIX 名称可能被误解为完整 `stat` 兼容 → Mitigation: 规格、头文件和文档明确这是 BigOS bounded metadata subset，未支持字段必须零填充或不暴露。
- [Risk] exFAT 与 `/rw` 后端可提供字段不同 → Mitigation: VFS 层定义统一字段来源和默认值，只要求后端提供可稳定解释的最小集合。
- [Risk] 元数据在 open fd、unlink、目录变更后出现不一致 → Mitigation: fd 查询绑定 open file object 当前引用，路径查询绑定当前目录项解析结果，行为验证覆盖 unlink/open 引用边界中可观察的最小结果。
- [Risk] 用户 ABI 结构体未来扩展困难 → Mitigation: 固定字段宽度、保留零填充字段，并要求内核复制前初始化完整结构。
- [Risk] 运行时 smoke 依赖本地 QEMU/Bochs 或交叉工具链 → Mitigation: 验证记录工具不可用和跳过原因，源码/行为断言作为最低可执行检查。

## Migration Plan

- 先定义公共 ABI、内核内部 metadata snapshot 和 errno 映射，不改变既有 open/read/write/lseek/fsync 行为。
- 再为 VFS、exFAT、`/rw` 和 fd table 查询接入统一填充路径，保持只读和可写后端原有边界。
- 然后补齐 libc wrapper、公共头、小型用户程序或 shell 消费路径。
- 最后补充行为验证和文档说明，确认未扩大 POSIX、持久化、相对路径、符号链接或设备节点承诺。
- 回退策略是移除新增 syscall 暴露、libc wrapper 和用户工具，保留既有 fd/VFS 和文件系统行为不变。

## Open Questions

- 无。第一版决策为：用户可见对象编号保持零值；用户态消费采用独立小型 `stat` 风格程序；目录 fd 的 `fstat` 只覆盖已有目录 open file object 或最小目录枚举消费路径，不额外扩大目录打开语义。
