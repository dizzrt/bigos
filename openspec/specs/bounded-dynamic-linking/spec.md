## Purpose

定义 BigOS 默认关闭的有界动态链接能力：在普通非 IRQ 内核上下文提供一条受限的动态链接装载路径，接受恰好含一个 `PT_INTERP` 的有界 `ET_DYN` 主镜像，经既有内核 VFS 读路径加载 `PT_INTERP` 指定的用户态解释器（ld.so），把主镜像与解释器的 `PT_LOAD` 段映射到确定性、有界、非冲突的加载基址，并以解释器入口进入 ring3；同时提供有界、freestanding-safe 的用户态动态链接器及其自重定位、有界共享对象加载与重定位/符号绑定子集，并交付端到端示例共享库与动态可执行程序。该能力不实现完整 POSIX 动态链接器：不提供 lazy PLT 运行时解析、TLS 重定位、`IFUNC`/`IRELATIVE`、符号版本、无界搜索路径或无界共享对象数量，且 MUST NOT 改变既有静态 `ET_EXEC` 装载路径、其 bounded 限制、ABI 或默认启动行为。

## Requirements

### Requirement: 内核有界动态镜像加载与解释器移交

BigOS SHALL provide a default-off bounded dynamic-link load path that accepts an `ET_DYN` main executable image containing exactly one `PT_INTERP` program header and loads the `PT_INTERP`-named user-space interpreter (ld.so). 该路径 MUST 仅在 ordinary 非 IRQ 内核上下文运行，MUST 把主镜像与解释器的 `PT_LOAD` 段分别映射到确定性、有界、非冲突的加载基址，并 MUST 以解释器入口（而非主镜像入口）作为 ring3 entry point。该路径 MUST NOT 改变既有静态 `ET_EXEC` 装载路径、其 bounded 限制或其 ABI。

#### Scenario: 加载动态主镜像与解释器

- **WHEN** 动态链接路径启用，且被加载镜像是含恰好一个 `PT_INTERP` 的有界 `ET_DYN` 可执行程序
- **THEN** BigOS MUST 校验主镜像头与 program header，经既有内核 VFS 读路径读取 `PT_INTERP` 指定的解释器 ELF（其自身为有界 `ET_DYN`）
- **AND** BigOS MUST 把主镜像 `PT_LOAD` 段映射到确定性主镜像基址、把解释器 `PT_LOAD` 段映射到确定性解释器基址，二者地址范围 MUST 落在受支持用户低半区且互不重叠，也不与栈/堆/匿名/file-backed 映射区冲突

#### Scenario: ring3 入口为解释器入口

- **WHEN** 动态主镜像与解释器均成功映射且初始栈构造完成
- **THEN** BigOS MUST 以解释器加载基址加 `e_entry` 作为 ring3 入口进入用户态
- **AND** BigOS MUST NOT 直接以主镜像入口进入 ring3

#### Scenario: 解释器缺失或非法的确定性失败

- **WHEN** `PT_INTERP` 指定路径不存在、超过有界装载限制、读取失败，或解释器不是受支持的有界 `ET_DYN`，或主镜像含多于一个 `PT_INTERP`
- **THEN** BigOS MUST 以确定性 loader/exec 错误或统一 panic 路径失败，MUST NOT 进入 ring3
- **AND** 失败路径上已分配的页、页表页、内核缓冲与 VMA 元数据 MUST 经既有 safe-release 路径回收

#### Scenario: 默认关闭不改变静态启动

- **WHEN** 动态链接构建开关关闭
- **THEN** 正常内核启动 MUST 不依赖任何动态链接组件，MUST 继续以静态 `ET_EXEC` 路径加载 `/bin/*` 与 PID-1 init
- **AND** 内核 MUST 对 `PT_INTERP`/`ET_DYN` 动态镜像保持既有确定性拒绝行为

### Requirement: 动态镜像段映射保留既有安全校验

BigOS SHALL apply, to the dynamic load path, the same explicit user page-attribute, bounds, overlap, alignment, and W^X safety checks used by the existing static loader, adjusted only for position-independent base-offset mapping. 主镜像与解释器的每个 `PT_LOAD` 段 MUST 按 `load_base + p_vaddr` 映射，且 `p_vaddr` 相对偏移、`p_memsz`/`p_filesz` 关系与权限 MUST 经既有校验。该路径 MUST NOT 发布 W+X、越界或权限歧义的用户映射。

#### Scenario: 动态段按基址偏移映射且权限明确

- **WHEN** 一个动态镜像的 `PT_LOAD` 段被映射
- **THEN** BigOS MUST 按 `load_base + p_vaddr` 计算映射地址，按 ELF 段 flags 派生用户页权限（可执行段清 NX 且不可写、可写数据/BSS 置 NX）
- **AND** `p_memsz` 超出 `p_filesz` 的字节 MUST 在进入用户态前清零

#### Scenario: 动态段不安全权限被拒绝

- **WHEN** 动态路径将产生 W+X 用户页、与其他段权限不兼容的重叠，或别名内核物理内存
- **THEN** BigOS MUST 确定性拒绝该镜像或失败该加载路径，MUST NOT 以 W+X 或歧义映射进入 ring3

### Requirement: 初始栈 auxiliary vector 握手

BigOS SHALL extend the initial user stack, for the dynamic load path, with a bounded auxiliary vector (auxv) appended after the existing `argc`/`argv`/`NULL`/`envp`/`NULL` layout and terminated by `AT_NULL`. auxv MUST 至少提供 `AT_PHDR`、`AT_PHENT`、`AT_PHNUM`、`AT_ENTRY`、`AT_BASE`、`AT_PAGESZ` 与 `AT_NULL`，使用户态解释器能定位主镜像 program header、主镜像真实入口、解释器自身加载基址与页大小。该扩展 MUST 是追加式，MUST NOT 改变既有 `argc`/`argv`/`envp` 的相对布局或破坏既有静态 crt0 的读取假设。

#### Scenario: auxv 提供解释器所需信息

- **WHEN** 动态主镜像与解释器映射完成、内核构造初始用户栈
- **THEN** BigOS MUST 在 `envp` 的 NULL 终止符之后写入有界 auxv，且 `AT_PHDR` MUST 指向主镜像在内存中的 program header，`AT_ENTRY` MUST 为主镜像真实入口，`AT_BASE` MUST 为解释器加载基址，并以 `AT_NULL` 终止
- **AND** 主镜像 program header MUST 位于已映射的用户可读内存内

#### Scenario: 静态路径对 auxv 透明

- **WHEN** 内核以静态 `ET_EXEC` 路径构造初始栈
- **THEN** 既有静态 crt0 MUST 仍能正确读取 `argc`/`argv`/`envp`，不受 auxv 扩展影响
- **AND** 静态路径 MUST NOT 因 auxv 扩展产生越界或权限歧义的栈写入

#### Scenario: auxv 构造失败回滚

- **WHEN** auxv 加上 `argv`/`envp`/字符串区的总大小、对齐或地址算术超出有界栈/参数区策略
- **THEN** BigOS MUST 在发布新镜像前确定性失败，MUST NOT 进入 ring3
- **AND** 失败路径上已分配资源 MUST 经既有 safe-release 路径回收

### Requirement: 用户态动态链接器自重定位与移交

BigOS SHALL provide a bounded, freestanding-safe user-space dynamic linker (ld.so) built position-independent. ld.so MUST 在其入口先完成**自重定位**（仅处理自身 `R_X86_64_RELATIVE`），在自重定位完成前 MUST NOT 访问依赖重定位的全局量或调用依赖 GOT 的函数；随后 MUST 经初始栈 auxv 定位主镜像与自身基址，完成共享对象加载与重定位后 MUST 跳转到主镜像真实入口，并按 System V x86_64 约定传递原始初始栈。ld.so MUST 仅通过本仓库有界 libc 或直接 `int 0x80` 与内核交互，MUST NOT 依赖宿主 runtime、异常、RTTI 或 C++ 全局构造。

#### Scenario: ld.so 自重定位先于其它工作

- **WHEN** 内核以解释器入口进入 ring3 并把含 auxv 的初始栈交给 ld.so
- **THEN** ld.so MUST 先由 `AT_BASE` 求得自身加载基址并仅处理自身 `R_X86_64_RELATIVE` 完成自重定位
- **AND** 在自重定位完成前 ld.so MUST NOT 访问需要重定位的全局符号或调用依赖 GOT 的函数

#### Scenario: ld.so 移交主镜像入口

- **WHEN** 主镜像与全部 `DT_NEEDED` 共享对象加载且重定位完成
- **THEN** ld.so MUST 跳转到 `AT_ENTRY` 指向的主镜像真实入口
- **AND** ld.so MUST 按 System V x86_64 约定把含 auxv 的原始初始栈传递给主程序，使主程序 crt0 能正确读取 `argc`/`argv`/`envp`

#### Scenario: ld.so 确定性失败不跳转未定义地址

- **WHEN** 解释器无法定位主镜像 `PT_DYNAMIC`、无法加载某个 `DT_NEEDED` 库、存在未解析的非弱符号，或任一有界上限被突破
- **THEN** ld.so MUST 发出确定性诊断 marker 并经 `SYS_EXIT` 退出
- **AND** ld.so MUST NOT 跳转到未重定位或未定义地址

### Requirement: 有界共享对象加载与重定位/符号绑定子集

BigOS ld.so SHALL load a bounded number of `DT_NEEDED` shared objects into a bounded position-independent mapping region and apply a bounded relocation and symbol-binding subset using eager (`BIND_NOW`-equivalent) binding. 支持的重定位类型 MUST 限定为 `R_X86_64_RELATIVE`、`R_X86_64_GLOB_DAT`、`R_X86_64_JMP_SLOT` 与 `R_X86_64_64` 子集；符号解析 MUST 在有界全局作用域内进行（主镜像优先，其后按加载顺序的已加载对象）。ld.so MUST NOT 实现 lazy PLT 运行时解析、TLS 重定位、`IFUNC`/`IRELATIVE`、符号版本或无界搜索路径；超出支持子集的重定位类型或超界条目 MUST 走确定性失败。

#### Scenario: 加载 DT_NEEDED 并绑定跨模块符号

- **WHEN** 动态主镜像声明有界数量 `DT_NEEDED` 共享库且这些库存在于约定搜索路径
- **THEN** ld.so MUST 把每个共享对象加载到有界共享对象映射区内的确定性基址，并解析其符号表
- **AND** ld.so MUST 对主镜像与各共享对象的 `R_X86_64_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`/`64` 重定位条目按 eager 绑定写入正确目标值，使主程序调用共享库导出符号时得到正确地址

#### Scenario: 未支持的动态特性确定性拒绝

- **WHEN** 镜像或共享对象包含 TLS 重定位、`IFUNC`/`IRELATIVE`、符号版本表，或超出支持子集的重定位类型，或 `DT_NEEDED`/共享对象数/重定位条目数超出有界上限
- **THEN** ld.so MUST 以确定性失败拒绝，发出诊断 marker 并退出
- **AND** ld.so MUST NOT 部分应用未支持的重定位或带着未解析跨模块引用跳转主程序入口

#### Scenario: 弱符号缺失的确定性处理

- **WHEN** 一个引用是弱符号（`STB_WEAK`）且在全局作用域内无定义
- **THEN** ld.so MUST 以确定性方式把该引用绑定为 0/未定义语义，MUST NOT 视为致命错误
- **AND** 非弱符号缺失 MUST 走确定性失败路径

### Requirement: 示例共享库与动态可执行程序端到端契约

BigOS SHALL deliver, under the default-off dynamic-link build, one example shared library and one dynamic executable that exercise the full path. 动态可执行程序 MUST 为 `ET_DYN` + `PT_INTERP`、`DT_NEEDED` 引用该示例共享库，并调用其导出符号；该端到端路径 MUST 经真实内核 VFS 读取与 ring3 执行验证跨模块符号解析。示例产物与解释器 MUST 仅在动态链接开关启用时构建与打包，MUST NOT 改变默认 `/bin/*` 静态程序集合或默认启动。

#### Scenario: 动态示例程序调用共享库导出符号

- **WHEN** 动态链接验证启用，内核加载动态可执行示例程序
- **THEN** 经 ld.so 加载示例共享库并完成重定位后，示例程序 MUST 成功调用共享库导出的函数/数据符号并得到预期结果
- **AND** 验证 MUST 发出确定性通过 marker

#### Scenario: 动态组件缺失或绑定失败的确定性失败

- **WHEN** 解释器、示例共享库缺失，或导出符号无法解析
- **THEN** 该路径 MUST 以确定性失败 marker 或统一 panic 体现，MUST NOT 静默成功或进入未定义执行

#### Scenario: 默认关闭不打包动态产物

- **WHEN** 动态链接构建开关关闭
- **THEN** 构建 MUST NOT 产出或打包 ld.so、示例共享库或动态可执行程序
- **AND** 默认磁盘镜像、默认 `/bin/*` 静态集合与默认启动行为 MUST 保持不变
