# 系统调用入口（syscall entry）

BigOS 阶段 6 前置：建立一条受控的“软件主动进入内核”路径与最小 syscall ABI。本阶段只做 syscall 入口、ABI 与 dispatch 的建立（1~2 个诊断 syscall），**不进入 ring3、不切换 CR3、不加载用户 ELF、不实现进程模型或完整 syscall 表**。

## 入口机制选择：`int 0x80` 软件中断门

本阶段采用 `int 0x80` 软件中断门，而非 `syscall`/`sysret` 快速系统调用：

- 复用既有 kernel-owned 静态 IDT + `interrupt.s` 的 `isr_common` + `irq_dispatch` 框架。vector `0x80` 的 `isr_entry` stub 与 dispatch 框架已存在，因此几乎是“零新增汇编”的入口：只需在 `irq_dispatch` 中识别 syscall vector 并路由到 syscall dispatcher。
- 取舍：`syscall`/`sysret` 需要配置 `IA32_STAR/LSTAR/FMASK` MSR、定义内核/用户段排列约束、并准备内核栈切换（无 TSS/`swapgs` 基础设施）。这些都与 ring3 切换强耦合，本阶段无 ring3，收益低、改动面大、风险高。留待 ring3 切换 / 用户程序加载 change 评估是否切换到 `syscall`。
- DPL：本阶段从 ring0 触发 `int 0x80`，现有 DPL=0 中断门即可工作，**不修改 IDT gate 的 DPL**。未来要允许 ring3 触发 `int 0x80`，需把该 vector gate 的 DPL 提升到 3——**这属于后续 ring3 change 的显式范围**，本阶段不提前改动。
- vector 用具名常量 `VECTOR_SYSCALL = 0x80` 固定，集中声明在 `include/irq/interrupt.h`，避免散落魔数。

## 最小 syscall ABI

syscall number、参数、返回值与 `InterruptFrame` 字段的对应关系（声明在 `include/bigos/syscall.h`，并由源码级检查断言）：

| 角色          | 寄存器 | `InterruptFrame` 字段 |
| ------------- | ------ | --------------------- |
| syscall number | `rax`  | `InterruptFrame.rax`  |
| 参数 0        | `rdi`  | `InterruptFrame.rdi`  |
| 参数 1        | `rsi`  | `InterruptFrame.rsi`  |
| 参数 2        | `rdx`  | `InterruptFrame.rdx`  |
| 参数 3        | `r10`  | `InterruptFrame.r10`  |
| 参数 4        | `r8`   | `InterruptFrame.r8`   |
| 参数 5        | `r9`   | `InterruptFrame.r9`   |
| 返回值        | `rax`  | dispatcher 写回 `InterruptFrame.rax` |

- syscall number 通过 `rax` 传入；返回值通过 `rax` 写回，即 dispatcher 写 `InterruptFrame.rax`，调用方在 `iretq` 返回后从 `rax` 读取结果。
- 第 4 个参数使用 `r10`（而非 `rcx`），贴近 SysV/Linux x86_64 syscall 约定，并避免与 `int 0x80` / `iretq` 下被破坏的 `rcx` 语义冲突。
- 除返回值外的寄存器约定为 callee 可 clobber，调用方负责保存。
- ABI 与具体入口机制解耦：dispatcher 以 `InterruptFrame` 为输入；未来若换 `syscall`/`sysret` 只需替换入口 stub，ABI 与 dispatch 可复用。

## dispatch 与未知 number 处理

`bigos::sys::dispatch(InterruptFrame*)`：

- 从 `InterruptFrame.rax` 读取 number，用 bounded switch 路由到内核实现。
- 已知 number 调用对应实现，返回值经 `rax` 写回。
- 未知 number 在 `rax` 写入确定性负错误码 `SYS_ENOSYS = -38`（等价 `-ENOSYS`），不崩溃、不进入 CPU 异常路径。
- 在 `irq_dispatch` 中通过 `is_syscall_vector(vector == VECTOR_SYSCALL)` 识别 syscall，命中后调用 `bigos::sys::dispatch` 并直接返回。该路径 **MUST NOT** 发送 i8259 EOI（syscall 不是外部 IRQ）；CPU 异常、外部 IRQ、syscall 三类入口的 EOI 语义保持分离不变。

## 诊断型 syscall

- `SYS_DEBUG_WRITE`（number=0）：把内核内固定/受限 buffer 经现有 serial/console 输出确定性 marker `BIGOS_SYSCALL_WRITE`，并返回写出的字节数。本阶段调用方为内核态，buffer 为内核内 bounded 来源；**不做用户指针校验**。
  - **ring3 前置项**：引入 ring3 后，用户态传入的 buffer 指针与长度 **必须** 做用户地址空间范围校验与 bounded 拷贝后才能输出。
- `SYS_GET_TICK`（number=1）：返回 `bigos::timer::ticks()` 单调 tick，验证返回值寄存器路径。`timer::ticks()` 已通过 `include/bigos/timer.h` 稳定暴露，是 context-agnostic bounded read，故选用它而非 `SYS_DEBUG_NOOP`。

诊断 syscall 遵守阶段 3 中断上下文契约：dispatcher 在 `int 0x80` 上下文运行（CPU 已自动关中断进入门），只做 bounded 输出/读取，不做动态分配、不在该路径调用 non-IRQ-safe allocator（`kmalloc`/`free`/`alloc_kernel_pages`/`free_pages`/global `new/delete`）。

## 验证：默认关闭构建开关 + 确定性 marker

新增默认关闭的 xmake 开关 `syscall_smoke`（`xmake f --syscall_smoke=y`）。开启后，`kernel()` 在非中断上下文从内核态发起几次 `int 0x80` 自测（设置 `rax`=number 与参数寄存器，执行 `int $0x80`，读取返回 `rax`），断言 dispatcher 被命中、`SYS_DEBUG_WRITE`/`SYS_GET_TICK` 返回值正确、未知 number 返回 `SYS_ENOSYS`，并输出确定性 marker `BIGOS_SYSCALL_SMOKE_PASSED` / `BIGOS_SYSCALL_SMOKE_FAILED`。默认 boot 不编入该路径。

## 本阶段非目标

- 不进入 ring3、不从用户态触发 syscall、不切换 CR3。
- 不加载用户 ELF、不实现进程模型 / fork/exec/signal / 用户线程。
- 不实现完整 syscall 表与 POSIX 语义；不实现 demand paging / COW，`#PF` 保持诊断-only。
- 不修改 IDT gate 的 DPL，不引入 GDT user/TSS 条目或 `syscall`/`sysret` MSR(STAR/LSTAR/FMASK) 配置。

## 横切工程化项

本 change 未修改 `tools/boot_debug.py`。若后续需要它自动注入 `syscall_smoke` 开关并观测 `BIGOS_SYSCALL_*` marker，应作为单独的横切工程化项处理，不把 Python 修改混入本 change，除非明确扩展任务范围。
