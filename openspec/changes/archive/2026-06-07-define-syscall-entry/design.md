## Context

阶段 5 `prepare-user-address-space-vmem` 已归档：内核已有显式页属性 primitive、user/kernel 属性策略，以及
“复制高半区、低半区独立”的用户地址空间页表根派生（不切 CR3、不进入 ring3）。当前内核只有两类入口：

- CPU 异常（vector 0x00–0x1f），由 `irq_dispatch` 路由到 exception handler（`#PF` 诊断-only）。
- i8259 外部 IRQ（vector 0x20–0x2f），EOI 仅对外部 IRQ 发送。

关键现状（来自 `src/kernel/irq/`）：

- IDT 为 kernel-owned 静态表，`__detail::initIDT()` 把全部 256 个 vector 都装成
  `PRESENT_RING0_INTERRUPT_GATE = 0x8e00`（present、type=interrupt gate、**DPL=0**），selector 为
  `KERNEL_CODE_SELECTOR = 0x08`。
- `interrupt.s` 已经为全部 256 个 vector（含 **0x80**）生成 `isr_entry` stub：push 合成 error code + vector，
  跳 `isr_common`，构建稳定 `InterruptFrame`、16 字节对齐后 `call irq_dispatch`，返回后 `iretq`。
- `irq_dispatch(InterruptFrame*)` 当前只区分 `is_cpu_exception` 与 `is_i8259_external_irq`，其余落到
  `unknown_vector_handler`（打印 marker、不崩溃、直接 `iretq` 返回）。

因此 `int 0x80` 软件中断在本工程里几乎是“零新增汇编”的入口：vector 0x80 的 stub 与 dispatch 框架已存在，
只需在 `irq_dispatch` 中识别 syscall vector 并路由到 syscall dispatcher。

约束：单核、早期关中断、无 SMP、无 ring3；freestanding C++17、无异常/无 RTTI；保持 kernel-owned 静态 IDT、
`InterruptFrame` dispatch ABI、exception 与外部 IRQ 的 EOI 分离不变；不得改动 boot 固定地址、higher-half/load
base、`KVMEM_BASE`、direct map、self-mapping 地址与 BootInfo handoff ABI。

## Goals / Non-Goals

**Goals:**

- 选定 syscall 入口机制并说明取舍；建立一条受控的“软件主动进入内核”路径。
- 定义最小 syscall ABI：syscall number 寄存器、参数寄存器顺序、返回值寄存器、clobber/保留约定，并明确其与
  `InterruptFrame` 字段的对应。
- 引入 syscall dispatch 层，按 number 路由；未知 number 返回确定性错误码而非崩溃或落入异常路径。
- 实现 1~2 个诊断型 syscall（输出确定性 marker / 返回固定值或 tick），作为 ring3 阶段前的内核态自测路径。
- 默认关闭构建开关 + 确定性 marker，源码级必测、emulator smoke 可选；默认 boot 行为不变。

**Non-Goals:**

- 不进入 ring3、不从用户态触发 syscall、不切换 CR3。
- 不加载用户 ELF、不实现进程模型 / fork/exec/signal / 用户线程。
- 不实现完整 syscall 表与 POSIX 语义；不实现 demand paging / COW，`#PF` 保持诊断-only。
- 不引入 GDT user/TSS 条目或 MSR(STAR/LSTAR/SFMASK) 配置（若选 `int 0x80` 路径）。

## Decisions

### 决策 1：本阶段采用 `int 0x80` 软件中断门，而非 `syscall`/`sysret`

复用既有 IDT + `isr_common` + `irq_dispatch` 框架：在 `irq_dispatch` 中新增 `is_syscall_vector(vector)`
（vector == `VECTOR_SYSCALL = 0x80`）判断，命中则调用 syscall 入口函数（如 `bigos::sys::dispatch(InterruptFrame*)`），
**不发送 i8259 EOI**（syscall 不是外部 IRQ），返回后经 `isr_common` 的 `iretq` 回到调用点。

- 备选：`syscall`/`sysret` 快速系统调用。否决理由：需要配置 `IA32_STAR/LSTAR/FMASK` MSR、定义内核/用户段
  排列约束、并准备内核栈切换（无 TSS/`swapgs` 基础设施）。这些都与 ring3 切换强耦合，本阶段无 ring3，收益低、
  改动面大、风险高。留待 ring3 切换 / 用户程序加载 change 评估是否切换到 `syscall`。
- DPL 说明：本阶段从 ring0 触发 `int 0x80`，现有 DPL=0 中断门即可工作，**不修改 IDT gate 的 DPL**。未来要
  允许 ring3 触发 `int 0x80`，需把该 vector gate 的 DPL 提升到 3——这属于 ring3 change 的显式范围，本阶段
  在 design/spec 中记录为前置条件，不提前改动。

### 决策 2：最小 syscall ABI（基于 `InterruptFrame`）

- syscall number：`rax`。
- 参数：`rdi`、`rsi`、`rdx`、`r10`、`r8`、`r9`（贴近 SysV/Linux x86_64 约定，避免与 `int 0x80` 下被 CPU
  自动保存的语义冲突；本阶段先支持前若干个参数，以实现需要为准并文档化）。
- 返回值：`rax`（写回 `InterruptFrame.rax`，`iretq` 后调用方从 `rax` 读取）。
- 错误返回：未知 number 或参数非法时，`rax` 写入确定性负错误码（如 `-ENOSYS` 等价的固定值），不崩溃、不进入
  exception 路径。
- dispatcher 通过修改 `InterruptFrame.rax` 写回返回值；其余被调用方约定为 callee 可 clobber，调用方负责保存。
  该映射关系（number/参数/返回值 ↔ `InterruptFrame` 字段）在 `docs/arch` 文档化并由源码级测试断言。

### 决策 3：syscall dispatch 层与诊断 syscall

- 在 `bigos`（建议 `bigos::sys` 子命名空间）下提供 `dispatch(InterruptFrame*)`：读取 `rax` 作为 number，
  用一个小的 bounded 跳转表 / switch 路由到内核实现，未知 number 走统一错误返回。
- 诊断 syscall（1~2 个）：
  - `SYS_DEBUG_WRITE`：把内核内固定/受限 buffer 经现有 console/串口输出确定性 marker（如
    `BIGOS_SYSCALL_WRITE`）。本阶段调用方为内核态，buffer 为内核地址；**不做用户指针校验**（无 ring3），
    但 design 明确记录“引入 ring3 后必须加用户指针/长度校验”为后续前置项。
  - `SYS_DEBUG_NOOP` 或 `SYS_GET_TICK`：返回固定值或 `timer::ticks()` 单调 tick，验证返回值寄存器路径。
- syscall 实现遵循阶段 3 中断上下文契约：dispatcher 在 `int 0x80` 上下文中运行（CPU 已自动关中断进入门），
  诊断 syscall 只做 bounded 输出 / 读取，不做动态分配、不在该路径调用 non-IRQ-safe allocator。

### 决策 4：验证用默认关闭构建开关 + 确定性 marker

新增默认关闭的 xmake 开关（如 `syscall_smoke`）：在 `kernel()` 非中断上下文中从内核态发起一次 `int 0x80`
自测（设置 `rax`=number 与参数寄存器，执行 `int $0x80`，读取返回 `rax`），断言 dispatcher 被命中、返回值正确、
未知 number 返回错误码，并输出确定性 `BIGOS_SYSCALL_*` marker（成功/失败）。默认 boot 不编入该路径。

## Risks / Trade-offs

- [选 `int 0x80` 后续可能迁移到 `syscall`] → 缓解：把 number/参数/返回值 ABI 与具体入口机制解耦，dispatcher
  以 `InterruptFrame` 为输入；未来换 `syscall` 只需替换入口 stub，ABI 与 dispatch 可复用。
- [无用户指针校验] → 本阶段从内核态触发、buffer 为内核地址，暂不校验；缓解：spec/design 显式记录“ring3 阶段
  必须加用户指针与长度校验”，并把诊断 syscall 的 buffer 限制为内核内 bounded 来源。
- [DPL=0 门不允许 ring3 触发] → 本阶段刻意如此；缓解：明确记录提升 DPL 到 3 属于 ring3 change 范围，避免本阶段
  提前放宽内核入口权限。
- [vector 0x80 与未来其它用途冲突] → 用具名常量 `VECTOR_SYSCALL` 固定 0x80，集中在中断头声明，避免散落魔数。
- [emulator oracle 不稳定] → 与历史 change 一致：源码级检查必测，runtime smoke 可选，oracle 不可用时记录命令、
  失败点、历史 oracle 状态与剩余 bootability 风险。
- [中断/EOI 误处理] → syscall vector 不是外部 IRQ，**严禁**发送 i8259 EOI；源码级检查断言 syscall 路径不调用
  `send_eoi`，保持 exception/IRQ/syscall 三类 EOI 语义清晰分离。

## Migration Plan

1. 在中断头新增 `VECTOR_SYSCALL = 0x80` 常量与 syscall number/ABI 声明（公共头最小化）。
2. 在 `irq_dispatch` 增加 syscall vector 分支，路由到 `bigos::sys::dispatch`；不动 exception/IRQ 既有分支与 EOI 语义。
3. 实现 dispatch 层 + 1~2 个诊断 syscall + 统一错误返回。
4. 加默认关闭 `syscall_smoke` 开关与内核态自测路径、`BIGOS_SYSCALL_*` marker。
5. 补源码级测试与 `docs/arch` 文档。

回滚策略：syscall 分支、dispatch 层与诊断 syscall 均为新增，smoke 默认关闭；回滚只需移除 `irq_dispatch` 中的
syscall 分支，IDT/`InterruptFrame`/boot 路径未改，风险可控。

## Open Questions

- 诊断 syscall 选 `SYS_GET_TICK` 还是 `SYS_DEBUG_NOOP` 作为第二个？（实现阶段按是否已稳定暴露 `timer::ticks()`
  决定；二者皆可验证返回值寄存器路径。）
- 未来是否切换到 `syscall`/`sysret`？（留待 ring3 切换 / 用户程序加载 change 评估 MSR/段/TSS/`swapgs` 成本。）
