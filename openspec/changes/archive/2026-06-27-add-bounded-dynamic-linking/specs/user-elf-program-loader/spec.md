## MODIFIED Requirements

### Requirement: loader rejects unsupported dynamic images while preserving future hooks

BigOS SHALL accept, only on a default-off bounded dynamic-link load path, an `ET_DYN` main executable containing exactly one `PT_INTERP` program header by loading the named user-space interpreter and handing control to it, while continuing to reject all dynamic-runtime features outside that bounded subset. 当动态链接路径关闭时，BigOS MUST 保持既有行为：对 `PT_INTERP`、动态重定位需求、共享对象、`ET_DYN` 执行或运行时动态加载器一律确定性拒绝。即使动态路径启用，超出有界子集的动态特性（TLS、`IFUNC`、符号版本、多 `PT_INTERP`、无界 `DT_NEEDED`）MUST 仍被确定性拒绝；既有静态 `ET_EXEC` 路径与其 bounded 限制 MUST 保持不变。

#### Scenario: 动态路径关闭时仍确定性拒绝

- **WHEN** 动态链接构建开关关闭，且某 ELF 镜像包含 `PT_INTERP`、需要动态重定位、是共享对象、是 `ET_DYN` 镜像或需要运行时动态加载器
- **THEN** BigOS MUST 以确定性 loader/exec 错误拒绝该镜像
- **AND** BigOS MUST NOT 创建带未解析动态运行时需求的可运行进程

#### Scenario: 动态路径启用时接受有界动态镜像

- **WHEN** 动态链接路径启用，且镜像是含恰好一个 `PT_INTERP` 的有界 `ET_DYN` 可执行程序，解释器存在且为受支持的有界 `ET_DYN`
- **THEN** BigOS MUST 按确定性基址加载主镜像与解释器并以解释器入口进入 ring3
- **AND** BigOS MUST 保留越界、重叠、对齐与 W^X 安全校验，仅按基址偏移调整地址校验

#### Scenario: 动态路径启用时仍拒绝超界特性

- **WHEN** 动态路径启用，但镜像或其依赖包含 TLS 重定位、`IFUNC`、符号版本、多于一个 `PT_INTERP`，或 `DT_NEEDED`/共享对象/重定位条目超出有界上限
- **THEN** BigOS 或其用户态解释器 MUST 以确定性失败拒绝，MUST NOT 进入 ring3 或带着未解析引用执行主程序

#### Scenario: future loader metadata is inert

- **WHEN** loader metadata includes reserved fields for future interpreter, shared-object, relocation, or runtime-linker handoff data that are not part of the enabled bounded dynamic-link path
- **THEN** current BigOS MUST leave those fields inert and unavailable to user execution
- **AND** no reserved metadata field may cause extra mappings, widened permissions, or dynamic-loader entry without the explicit bounded dynamic-link path

### Requirement: initial stack and argument layout is bounded

BigOS SHALL require the loader/exec path to construct initial user stack, `argv`, `envp`, and (on the dynamic load path) a bounded auxiliary vector within the committed runtime VM layout using bounded sizes and deterministic failure behavior. auxv MUST 在 `envp` 的 NULL 终止符之后追加并以 `AT_NULL` 终止，MUST NOT 改变 `argc`/`argv`/`envp` 的相对布局或破坏既有静态 crt0 的读取假设。

#### Scenario: argument stack setup succeeds within bounds

- **WHEN** exec prepares `argv` and `envp` for a bounded static user image
- **THEN** BigOS MUST place the initial stack data inside the allowed user stack/runtime argument area with correct user permissions and alignment for the established ABI
- **AND** entry into ring3 MUST use the committed stack pointer and entry point from the runtime image description

#### Scenario: 动态镜像初始栈追加有界 auxv

- **WHEN** exec 在动态加载路径为 `ET_DYN` + `PT_INTERP` 主镜像准备初始栈
- **THEN** BigOS MUST 在 `envp` 的 NULL 终止符之后写入有界 auxv（至少 `AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL`）于已提交 runtime VM 布局内
- **AND** 既有静态 crt0 对该追加式 auxv MUST 保持透明，仍能正确读取 `argc`/`argv`/`envp`

#### Scenario: argument stack setup failure rolls back

- **WHEN** argument count, environment count, string length, auxv size, stack size, alignment, or address arithmetic exceeds the bounded loader policy
- **THEN** BigOS MUST fail exec deterministically before publishing the new image
- **AND** the old process image MUST remain active unless the process has entered a documented fatal exec path
