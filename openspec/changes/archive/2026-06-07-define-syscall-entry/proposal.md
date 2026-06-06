## Why

阶段 5 的 `prepare-user-address-space-vmem` 已归档完成：内核已经有显式页属性 primitive、user/kernel
属性策略，以及“复制内核高半区、低半区独立”的用户地址空间页表根派生（但本阶段不切 CR3、不进入 ring3）。
要继续向阶段 6（加载并运行第一个用户程序）推进，下一块缺口是“内核态如何被显式、受控地进入”：当前内核
只有 i8259 外部 IRQ 与 CPU 异常两类入口，没有任何供软件主动触发的系统调用入口与最小 ABI。

本 change 只建立 syscall 入口边界与最小调用约定（先做 1~2 个诊断型 syscall），为后续 ring3 切换与用户程序
加载预留稳定的内核进入点，**但不实现 ring3 切换、不加载用户 ELF、不实现进程模型或完整 syscall 表**。

## What Changes

- 选定并定义一个系统调用入口机制：在 `int 0x80` 软件中断门与 `syscall`/`sysret` 快速系统调用之间二选一，
  并在 design 中说明取舍。当前内核为 kernel-owned 静态 IDT、单核、早期关中断、无 ring3，倾向先用 `int 0x80`
  软件中断门，复用现有 `InterruptFrame` dispatch ABI 与 ISR common path，降低对 boot/GDT/MSR 路径的改动面。
- 定义最小 syscall ABI：syscall number 放入约定寄存器（如 `rax`），参数寄存器顺序、返回值寄存器（如 `rax`）、
  以及 clobber/保留寄存器约定；明确该 ABI 与现有 `InterruptFrame` 寄存器布局的对应关系。
- 引入 syscall 分发层（`bigos` 命名空间下的 syscall dispatch），按 syscall number 路由到内核实现；未知 syscall
  number 返回确定性错误码而非崩溃。
- 实现 1~2 个诊断型 syscall（例如 `sys_debug_write`：把内核内 buffer 经 console/串口输出确定性 marker；
  `sys_debug_noop` 或 `sys_get_tick`：返回单调 tick / 固定值），供 ring3 阶段前从内核态自测调用路径。
- 新增默认关闭的构建开关与确定性 `BIGOS_` marker，用源码级检查 +（可选）emulator smoke 验证入口 wiring、
  ABI 寄存器约定与 dispatch 行为；默认 boot 行为不变。

非目标（明确不在本 change 范围内）：

- 不切换到 ring3、不从用户态实际触发 syscall（本阶段仅从内核态/同 ring 自测入口路径与 dispatch）。
- 不加载用户态 ELF、不实现 fork/exec/signal、不实现进程模型或用户线程。
- 不实现完整 syscall 表（如 read/write/open/mmap 等 POSIX 语义），仅 1~2 个诊断 syscall。
- 不实现 demand paging / copy-on-write，`#PF` handler 保持诊断-only。
- 不改动 boot 固定地址、higher-half/load base、`KVMEM_BASE`、direct map 区域、self-mapping 地址或 BootInfo
  handoff ABI；若选 `int 0x80` 路径则不引入新的 GDT user/TSS 条目或 MSR 配置（留待 ring3 切换 change）。

## Capabilities

### New Capabilities

- `syscall-entry`: 系统调用入口机制（`int 0x80` 软件中断门或 `syscall`/`sysret`，二选一并说明取舍）、最小
  syscall ABI（number/参数/返回值/clobber 寄存器约定）、syscall dispatch 层与未知 number 的错误返回、1~2 个
  诊断型 syscall，以及对应的默认关闭构建开关、确定性 marker、源码级与 emulator 验证要求。

### Modified Capabilities

（无。本 change 不改变已归档 capability 的既有 requirement。若采用 `int 0x80` 软件中断门，会在现有
`interrupt-exception-foundation` 的 dispatch 框架内新增一条受控入口路径，但不改变 kernel-owned 静态 IDT、
`InterruptFrame` ABI、exception 与外部 IRQ 的 EOI 分离等既有契约，因此不构成 spec-level 行为变更。）

## Impact

- 受影响子系统：中断/入口（`src/kernel/irq/` 下的 IDT 注册与 dispatch、`interrupt.s` ISR stub）、内核入口
  `src/kernel`（新增 syscall dispatch 与诊断 syscall 实现），以及公共头（新增 syscall number/ABI 声明）。
- 受影响假设：单核、早期关中断、无 SMP、无 ring3；保持 kernel-owned 静态 IDT 与 `InterruptFrame` dispatch ABI
  不变；syscall 入口路径在本阶段仅从内核态/同 ring 触发，不依赖用户态地址空间切换。
- 构建：新增默认关闭的 xmake 验证开关与 `BIGOS_` marker；不改变默认 boot 行为。
- 工具链 / emulator：沿用 `x86_64-elf-gcc` cross-build 与 Bochs serial/VGA oracle；runtime smoke 在 oracle
  不稳定时记录命令、失败点与剩余 bootability 风险，与历史 change 一致。
