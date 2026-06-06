## ADDED Requirements

### Requirement: 系统调用入口机制

BigOS SHALL 提供一条受控的软件触发内核入口路径，使内核代码可以通过约定的系统调用指令进入内核态的
syscall 处理逻辑，而不依赖外部 IRQ 或 CPU 异常。本阶段 SHALL 复用 kernel-owned 静态 IDT 与既有
`InterruptFrame` dispatch 框架，采用 `int 0x80` 软件中断门作为入口。

#### Scenario: syscall vector 被 dispatch 识别并路由

- **WHEN** 内核代码执行系统调用指令进入约定的 syscall vector（`VECTOR_SYSCALL = 0x80`）
- **THEN** 中断 dispatch MUST 把该 vector 识别为系统调用而非 CPU 异常或外部 IRQ
- **AND** dispatch MUST 把控制路由到 syscall 处理入口，并在处理返回后经既有 `iretq` 路径返回调用点

#### Scenario: syscall 路径不发送外部 IRQ EOI

- **WHEN** syscall vector 被处理
- **THEN** 该路径 MUST NOT 发送 i8259 EOI（syscall 不是外部 IRQ）
- **AND** CPU 异常、外部 IRQ 与 syscall 三类入口的 EOI 语义 MUST 保持分离不变

#### Scenario: 保持既有中断契约不变

- **WHEN** 引入 syscall 入口
- **THEN** kernel-owned 静态 IDT、`InterruptFrame` 字段布局与 dispatch ABI、exception 与外部 IRQ 的既有
  处理路径 MUST 保持不变
- **AND** 本阶段 MUST NOT 修改 IDT gate 的 DPL、MUST NOT 引入 GDT user/TSS 条目或 `syscall`/`sysret` MSR 配置

### Requirement: 最小系统调用 ABI

BigOS SHALL 定义并以源码级方式固定一个最小 syscall ABI，明确 syscall number、参数、返回值与错误返回所使用
的寄存器，并文档化其与 `InterruptFrame` 字段的对应关系。

#### Scenario: number 与返回值寄存器约定

- **WHEN** 内核代码发起一次 syscall
- **THEN** syscall number MUST 通过约定寄存器（`rax`）传入
- **AND** syscall 返回值 MUST 通过约定寄存器（`rax`）写回，即 dispatcher 写 `InterruptFrame.rax`，调用方在
  返回后从 `rax` 读取结果

#### Scenario: 参数寄存器顺序固定

- **WHEN** syscall 带有参数
- **THEN** 参数 MUST 按文档化的固定寄存器顺序（`rdi`、`rsi`、`rdx`、`r10`、`r8`、`r9`）从 `InterruptFrame`
  对应字段读取
- **AND** ABI 与 `InterruptFrame` 字段的对应关系 MUST 在 `docs/arch` 文档化并由源码级检查断言

### Requirement: 系统调用分发与未知 number 处理

BigOS SHALL 提供一个 syscall dispatch 层，按 syscall number 路由到内核实现，并对未知 number 或非法请求返回
确定性错误码，而不崩溃或落入异常路径。

#### Scenario: 已知 number 被路由到实现

- **WHEN** dispatcher 收到一个已注册的 syscall number
- **THEN** dispatcher MUST 调用对应的内核 syscall 实现
- **AND** 实现的返回值 MUST 经返回值寄存器写回调用方

#### Scenario: 未知 number 返回确定性错误码

- **WHEN** dispatcher 收到一个未注册的 syscall number
- **THEN** dispatcher MUST 在返回值寄存器写入一个确定性的负错误码（等价 `-ENOSYS`）
- **AND** dispatcher MUST NOT 崩溃、MUST NOT 进入 CPU 异常处理路径

### Requirement: 诊断型系统调用

BigOS SHALL 实现 1~2 个诊断型 syscall，用于在 ring3 阶段之前从内核态自测 syscall 入口、ABI 与 dispatch 路径。

#### Scenario: 诊断写 syscall 输出确定性 marker

- **WHEN** 内核代码调用诊断写 syscall（`SYS_DEBUG_WRITE`）并传入内核内 bounded buffer
- **THEN** 该 syscall MUST 经现有 console/串口输出确定性 `BIGOS_` marker
- **AND** 本阶段该 syscall MAY 不校验指针（调用方为内核态），但实现 MUST 把 buffer 限制为内核内 bounded 来源，
  并在文档/设计中记录引入 ring3 后必须加用户指针与长度校验

#### Scenario: 诊断 syscall 返回值路径可验证

- **WHEN** 内核代码调用返回固定值或单调 tick 的诊断 syscall（`SYS_DEBUG_NOOP` 或 `SYS_GET_TICK`）
- **THEN** 该 syscall MUST 通过返回值寄存器返回预期值
- **AND** 该返回值 MUST 可被源码级检查或自测路径断言

#### Scenario: 诊断 syscall 遵守中断上下文契约

- **WHEN** 诊断 syscall 在 `int 0x80` 上下文中执行
- **THEN** 该 syscall 实现 MUST 只做 bounded 输出或读取
- **AND** 该 syscall 实现 MUST NOT 在该路径调用 non-IRQ-safe allocator 或执行动态内存分配

### Requirement: 本阶段不进入用户态

BigOS SHALL 把本 change 限定为 syscall 入口、ABI 与 dispatch 的建立，本阶段 SHALL NOT 进入 ring3、SHALL NOT
切换 CR3、SHALL NOT 加载用户程序。

#### Scenario: syscall 仅从内核态触发

- **WHEN** 本阶段任何 syscall 自测或验证路径
- **THEN** syscall MUST 仅从内核态/ring0 触发
- **AND** BigOS MUST NOT 进入 ring3、MUST NOT 切换 CR3、MUST NOT 加载用户态 ELF
- **AND** `#PF` handler MUST 保持诊断-only，不做恢复

### Requirement: 系统调用入口的验证可复现

BigOS SHALL 用源码级检查与默认关闭的 emulator smoke 验证 syscall 入口 wiring、ABI 寄存器约定、dispatch
路由与未知 number 错误返回。

#### Scenario: 源码级检查覆盖入口与 ABI 不变量

- **WHEN** 本 change 实现完成
- **THEN** 源码级检查 MUST 覆盖：syscall vector 在 dispatch 中被识别且不发送 EOI、number/参数/返回值寄存器
  约定、已知 number 路由到实现、未知 number 返回确定性错误码、诊断 syscall 输出 marker / 返回预期值
- **AND** 源码级检查 MUST 确认本阶段不进入 ring3、不切换 CR3、不修改 IDT gate DPL、不引入 GDT user/TSS 或
  `syscall`/`sysret` MSR 配置

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** 实现完成
- **THEN** 验证 MUST 记录最窄可用的 `xmake` / cross-toolchain 构建、相关 `uv run pytest` 源码级检查，以及
  `openspec validate define-syscall-entry --strict`
- **AND** 若 Bochs runtime smoke 因 emulator、ROM、serial oracle、image lock 或交互限制无法观测 marker，
  验证 MUST 记录缺失依赖与剩余 bootability 风险
