## MODIFIED Requirements

### Requirement: future dynamic-linking preparation remains non-runtime

BigOS SHALL define explicit, bounded layout and metadata extension points for dynamic linking and SHALL, only on a default-off bounded dynamic-link path, use a bounded interpreter mapping region, shared-object mapping region, and auxv metadata as live runtime layout. 当动态链接路径关闭时，运行时布局 MUST 把这些预留 gap 保持为未映射的非运行时区域并继续拒绝不受支持的动态特性。即使动态路径启用，所有新增动态映射区 MUST 落在受支持用户低半区，MUST 与既有 ELF 映射、heap、受限匿名映射、用户栈、guard/growth、参数区互不重叠，且超出有界动态子集的特性 MUST 仍被拒绝。

#### Scenario: 动态路径关闭时 gap 仍非运行时

- **WHEN** 动态链接路径关闭，且某用户 ELF 镜像需要 `PT_INTERP`、动态重定位、共享对象加载、`ET_DYN` 执行或运行时动态加载器
- **THEN** BigOS MUST 以确定性 loader/exec 错误拒绝该镜像
- **AND** BigOS MUST NOT 以部分解释的动态镜像进入 ring3，预留 gap MUST 保持未映射

#### Scenario: 动态路径启用时 gap 成为有界运行时映射区

- **WHEN** 动态链接路径启用并加载含 `PT_INTERP` 的有界 `ET_DYN` 主镜像
- **THEN** BigOS MUST 把解释器映射区与共享对象映射区放入既有预留运行时 gap 内，并纳入 runtime VM 布局描述与 VMA 集合
- **AND** 所有新增动态映射区 MUST 页对齐、互不重叠、落在受支持用户低半区，并避免内核 higher-half/direct-map/KVMEM/自映射范围与栈冲突

#### Scenario: 动态布局冲突或超界拒绝提交

- **WHEN** 动态映射区准备时检测到 overflow、overlap、不支持的对齐、内核范围冲突、栈冲突，或动态子集之外的特性（TLS、`IFUNC`、符号版本、无界共享对象数）
- **THEN** BigOS MUST 在发布新进程布局前拒绝该镜像
- **AND** 失败尝试中已分配的页、页表页、内核缓冲或 VMA 元数据 MUST 经既有 safe-release 路径回收

#### Scenario: reserved future area does not grant access

- **WHEN** the runtime layout contains a reserved future-runtime gap not used by an enabled bounded dynamic-link path
- **THEN** BigOS MUST leave the gap unmapped and uncovered by writable/executable VMAs until an explicit capability defines its semantics
- **AND** user access to the gap MUST fail through the normal user fault path rather than materializing memory implicitly
