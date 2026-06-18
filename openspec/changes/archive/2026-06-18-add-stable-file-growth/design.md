## Context

BigOS 当前已经具备单核 x86_64、有界用户态、VFS、RAM-backed `/rw`、persistent clean-sync
`/rw`、目录树、metadata、受限 rename、page/buffer cache 和最小用户态工具。现有文件写入语义可以支撑基础运行期可见性，但下一步需要把常规文件从“可写”推进到“稳定增长”：扩展写、截断、块分配、块释放和 clean-sync 后读回必须形成可测试契约。

本设计影响文件系统和缓存路径，向上触及 fd/VFS、syscall wrapper、metadata 和用户态验证；向下复用当前同步块 IO、page/buffer cache、RAM-backed 后端和 persistent clean-sync 后端。它不跨越 boot、IRQ、页表或 linker 边界，不改变 `int 0x80` 寄存器 ABI、既有 syscall 编号顺序、Legacy BIOS/MBR/exFAT boot image 布局或 UEFI backend parity。

## Goals / Non-Goals

**Goals:**

- `/rw` 常规文件支持有界扩展写，包括追加写、偏移越过 EOF 后写入、跨块写入和多块读取。
- 截断支持收缩与扩展：收缩释放不再拥有的数据块，扩展区域读取为确定的零值。
- RAM-backed 与 persistent clean-sync `/rw` 使用同一块分配、释放和复用规则，避免块别名、重复释放和旧数据泄漏。
- 扩展写和截断的成功结果对 read、stat/fstat、dup/fork/exec 继承 fd、重新 open 路径和目录树状态一致可见。
- persistent clean-sync 后端在成功同步后，让扩展文件内容、截断后大小和块映射跨 clean reboot 可见。
- 容量耗尽、缓存块耗尽、内核分配失败、用户缓冲失败和块 IO 失败必须确定性返回，不发布半成品 size、块映射、dirty state 或 fd offset。
- 新增或扩展 default-off 验证，覆盖增长、截断、块复用、容量边界和 clean reboot 读回。

**Non-Goals:**

- 不实现完整 POSIX filesystem、完整 `ftruncate`/`truncate` 工具链、`pwrite`/`pread` 完整兼容、file-backed writable `mmap`、稀疏文件承诺或 hole preservation API。
- 不实现 journal、ordered-write 策略、完整 crash recovery、power-loss recovery 或未同步 dirty 状态持久化。
- 不引入 async I/O、宽泛块层框架、新存储/设备 backend、SMP 启用、动态链接或完整 POSIX libc。
- 不改变 boot handoff ABI、MBR/exFAT 启动资产、页表布局、中断向量、syscall ABI 或只读 exFAT 状态。

## Decisions

### 1. 文件增长以显式逻辑块映射为提交边界

扩展写先完成权限、文件类型、offset/length、最大文件大小、用户缓冲和容量预检，再为目标逻辑块准备映射。只有数据块内容、必要零填充和 inode size 都能完成时，才发布新的 size 和块映射。失败必须回滚本次新分配的块，并保留旧 size、旧块映射和旧 fd offset。

替代方案是先推进 size，再逐块补写。这会让失败路径暴露半成品 EOF 和未初始化块，不符合当前 bounded filesystem 的可解释失败要求，因此不采用。

### 2. EOF gap 读取返回零值，但不声明完整稀疏文件

当写入 offset 越过旧 EOF 时，旧 EOF 到写入起点之间的 gap 必须在后续读取中返回零值。实现可以选择物理分配并清零中间块，也可以在 inode 映射中保留可解释的未分配零块状态；无论采用哪种实现，都不得把旧块内容暴露给用户。

替代方案是拒绝越过 EOF 的写入。该方案简单，但会限制 shell 和小程序对 lseek+write 的基本文件增长用法，也不能充分验证块分配边界，因此不采用。

### 3. 截断采用“先准备新状态，后发布 inode”的顺序

收缩截断先构造目标 size 和保留块集合，发布 inode 后再释放不再拥有的块；扩展截断先确保新增范围的零读语义和容量边界可满足，再发布 size。任何失败都不得留下同时被旧文件和 free list 认为拥有的块，也不得提前暴露新 size。

替代方案是先释放块再更新 inode。该方式在中途失败或后续读取时容易产生 dangling block mapping，不适合缺少 journal 的 clean-sync 后端。

### 4. 块释放与复用必须清除所有权并防止旧数据泄漏

释放块必须从对应 inode 映射中移除后才可进入可复用集合；复用块在暴露给新文件或扩展区域前必须清零或完全覆盖。free-space metadata 对 RAM-backed 和 persistent 后端都必须保持单一所有权视角，避免块别名、重复释放和泄漏旧文件内容。

替代方案是允许复用块保持旧内容并依赖调用方覆盖。该方案会把部分写、短写或扩展截断变成信息泄漏风险，因此不采用。

### 5. page/buffer cache 是同步可见性的必经路径

扩展写和截断必须通过现有 page/buffer cache 表达 dirty state、fsync、显式同步和淘汰。`fsync` 成功前，persistent 后端不得声称 durable；写回失败必须保留脏状态或等价 pending-write 状态，并向调用方返回确定性错误。

替代方案是绕过缓存直接写后端。该方案会分裂 RAM-backed 与 persistent 行为，并削弱后续统一 writeback 路径，因此不采用。

### 6. 用户态接口保持最小增量

实现可以补齐最小 `truncate`/`ftruncate` wrapper 或路径工具以便验证，但接口必须保持 bounded libc 范围，不承诺完整 POSIX 参数、时间戳、副作用或错误全集。若新增 syscall，只能追加编号；若可复用既有 open truncate flags 和 fd 操作，则优先复用现有 ABI。

### 7. 验证沿用 default-off smoke 与 clean-sync 双启动边界

运行期验证覆盖 RAM-backed 文件增长、gap 零读、收缩/扩展截断、容量耗尽回滚、块复用不泄漏旧数据、fd/metadata 可见性。persistent 验证复用现有 clean-sync 双启动模型：第一阶段写入并同步，第二阶段用同一持久测试盘重启读回；该验证只证明 clean reboot 后同步状态可见。

## Risks / Trade-offs

- [Risk] 扩展写和截断会触发更多分配/回滚路径，容易出现块泄漏或重复释放。→ Mitigation: 以 inode 逻辑块映射为所有权真源，验证覆盖容量耗尽、截断释放和复用后再写。
- [Risk] EOF gap 零读若实现为未分配零块，会增加读取路径分支。→ Mitigation: 将零读语义限制在 `/rw` 常规文件的有界范围内，不对外承诺完整稀疏文件 API。
- [Risk] persistent 后端没有 journal，中途失败后状态复杂。→ Mitigation: 只承诺操作失败不发布半成品和成功同步后的 clean reboot 可见性，不声明 crash recovery。
- [Risk] cache 写回失败可能让 fd 已观察到的数据尚未 durable。→ Mitigation: 明确 current-runtime 可见性与 persistent durable 边界，`fsync` 失败不得报告 durable success。
- [Risk] 用户态 wrapper 名称可能让读者误解为完整 POSIX。→ Mitigation: 文档和规格写明 bounded subset，不承诺完整 POSIX `truncate`/`ftruncate`。

## Migration Plan

1. 审查现有 `/rw` 文件 size、块映射、free-space metadata、cache dirty state 和 fd offset 提交顺序。
2. 实现扩展写的容量预检、块准备、gap 零读、跨块写入和失败回滚。
3. 实现收缩/扩展截断、块释放复用和 metadata 可见性。
4. 将 RAM-backed 与 persistent clean-sync 后端接入同一稳定块分配语义和 page/buffer cache 写回边界。
5. 补齐最小用户态 wrapper/tool 与 default-off 验证，覆盖 RAM-backed 运行期和 persistent clean reboot 读回。
6. 更新相关 docs/en 与 docs/zh 时保持双语镜像一致，并避免把 bounded subset 描述为完整 POSIX。

Rollback 策略是关闭新增验证和用户态入口，回退到既有 bounded write/truncate 行为；任何修改 boot layout、exFAT 启动资产、页表布局或 syscall 编号重排的变更都应在 review 中阻断。

## Open Questions

当前无阻塞性待决问题。实现阶段可以在“物理分配清零 gap”与“未分配零块映射”之间选择，但必须满足同一零读、失败回滚和持久同步规格。
