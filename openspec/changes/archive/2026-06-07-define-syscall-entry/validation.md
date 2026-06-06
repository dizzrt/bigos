# Validation

## 已通过检查

- `uv run pytest tests/test_syscall_entry_source.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py`：30 passed。新增 `tests/test_syscall_entry_source.py` 覆盖：`VECTOR_SYSCALL = 0x80` 具名常量与 DPL=0 / ring3 后续说明、最小 syscall ABI 声明（number=`rax`、参数=`rdi/rsi/rdx/r10/r8/r9`、返回值=`rax`、`SYS_DEBUG_WRITE/SYS_GET_TICK/SYS_ENOSYS=-38`、`dispatch` 声明）、`docs/en/arch` ABI 映射文档化、`irq_dispatch` 识别 syscall vector 且不发送 EOI（`send_eoi` 仍仅 1 处）、dispatch 从 `InterruptFrame.rax` 读 number 并写回、已知 number 路由到实现、未知 number 返回 `SYS_ENOSYS` 且不崩溃/不进异常路径、`SYS_DEBUG_WRITE` 输出 `BIGOS_SYSCALL_WRITE` 且 buffer 为内核内 bounded 来源 + ring3 校验前置项、`SYS_GET_TICK` 经返回寄存器返回单调 tick、syscall 路径不调用 non-IRQ-safe allocator/无动态分配、本阶段不进 ring3 / 不切 CR3 / 不改 IDT gate DPL / 不引入 GDT user-TSS 或 `syscall`/`sysret` MSR、`syscall_smoke` 默认关闭且 marker wiring 在 `initIRQ()` 之后、`sched::start()` 之前。
- `uv run pytest tests/`：全量源码级测试通过，无回归（新增 13 项 syscall 测试，既有测试未受影响）。
- `xmake`（默认配置，`x86_64-elf-gcc` cross-toolchain）：构建通过，新增 `src/kernel/syscall/syscall.cc` 编译链接成功，默认 boot 行为不变。
- `xmake f --syscall_smoke=y && xmake`：syscall smoke 显式配置构建通过（编入 `kernel()` 内核态 `int 0x80` 自测路径）；随后 `xmake f -c` 恢复默认配置（smoke 默认关闭）。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/kernel/syscall/syscall.cc src/kernel/irq/interrupt.cc src/kernel/kernel.cc`：freestanding 语法检查通过，无告警。
- IDE diagnostics：`include/bigos/syscall.h`、`src/kernel/syscall/syscall.cc`、`src/kernel/irq/interrupt.cc`、`src/kernel/kernel.cc` 无诊断。
- 中断 ABI 安全复核（源码级）：syscall 分支沿用既有 `isr_common` 栈对齐与寄存器保存、`iretq` 返回路径不变（`interrupt.s` 未改动）；syscall 路径只调用 `bigos::sys::dispatch(__frame)` 并直接 `return`，不调用 `send_eoi`；`driver::irqchip::i8259::send_eoi` 在 `interrupt.cc` 仍仅出现 1 次（外部 IRQ 分支），exception/IRQ/syscall 三类 EOI 语义保持分离。
- `openspec validate define-syscall-entry --strict`：通过（Change is valid）。

## 未运行或未通过检查

- Bochs runtime smoke 未观测 `BIGOS_SYSCALL_SMOKE_PASSED` marker：`tools/boot_debug.py` 的 `build_kernel()` 只切换 `mm_self_test` 开关，不支持注入 `syscall_smoke` 构建开关，因此无法用现有 oracle 自动构建带 smoke 的 image 并观测 `BIGOS_SYSCALL_*` marker。按 tasks 4.3，本 change 不把 `tools/boot_debug.py` 的 Python 修改混入；该 oracle 扩展记录为后续独立横切工程化项。
- 普通 boot Bochs serial smoke 未尝试声明通过：与历史 change 一致，本机 Bochs/serial 组合不是可靠 oracle（见下）。

## 历史诊断

- Bochs serial oracle 在本仓库历史 change 中多次不稳定。归档记录 `openspec/changes/archive/2026-06-06-prepare-user-address-space-vmem/validation.md`、`.../introduce-kernel-threads-scheduler/validation.md`、`.../establish-tty-console-input/validation.md` 均记录构建通过但 Bochs serial smoke 在 30~40 秒内未观测到 marker。`docs/en/arch/interrupt-exception-foundation.md` 的“无法完成的 runtime smoke”同样记录普通 boot 与 memory self-test 的 serial marker 不可观测。因此本 change 未把 Bochs runtime smoke 失败视为本 change 引入的回归，而是既有 oracle 限制。

## 当前 Change 影响

- 修改 `include/irq/interrupt.h`：新增具名常量 `VECTOR_SYSCALL = 0x80`，并注释记录本阶段 DPL=0、提升到 DPL=3 属后续 ring3 change 范围。
- 新增 public header `include/bigos/syscall.h`：声明 `bigos::sys` 的 syscall number 枚举（`SYS_DEBUG_WRITE`/`SYS_GET_TICK`）、错误码 `SYS_ENOSYS = -38`、`dispatch(InterruptFrame*)` 入口，以及最小 syscall ABI（number/参数/返回值寄存器 ↔ `InterruptFrame` 字段）文档注释。
- 新增 `src/kernel/syscall/syscall.cc`：实现 `bigos::sys::dispatch`（从 `rax` 读 number、bounded switch 路由、未知 number 写回 `SYS_ENOSYS`、写回 `InterruptFrame.rax`），`SYS_DEBUG_WRITE`（输出 `BIGOS_SYSCALL_WRITE` 内核内 bounded buffer，记录 ring3 用户指针/长度校验前置项），`SYS_GET_TICK`（返回 `timer::ticks()`）。
- 修改 `src/kernel/irq/interrupt.cc`：新增 `is_syscall_vector()` 与 `irq_dispatch` 中的 syscall 分支，路由到 `bigos::sys::dispatch` 且不发送 i8259 EOI；exception/外部 IRQ 既有分支与 EOI 语义未改动。
- 修改 `src/kernel/kernel.cc`：在 IRQ 使能后、`sched::start()` 前的非中断上下文新增 `syscall_smoke()`（仅 `BIGOS_SYSCALL_SMOKE` 下），用 `int $0x80` 内核态自测 dispatcher 命中、返回值与未知 number 错误返回，输出 `BIGOS_SYSCALL_SMOKE_PASSED/FAILED`。
- 修改 `xmake.lua`：新增默认关闭的 `syscall_smoke` 开关与 `BIGOS_SYSCALL_SMOKE` define。
- 新增文档/测试：`docs/en/arch/syscall-entry.md`、`tests/test_syscall_entry_source.py`。
- 未改动 `src/kernel/irq/interrupt.s`、kernel-owned 静态 IDT、`InterruptFrame` 布局、IDT gate DPL、boot 固定地址、higher-half/load base、BootInfo handoff ABI、self-mapping 与 direct map 布局；未引入 GDT user/TSS 条目或 `syscall`/`sysret` MSR 配置。

## 剩余风险

- **syscall 入口 runtime 未经 oracle 验证**：`int 0x80` 入口 wiring、ABI 寄存器约定、dispatch 路由与未知 number 错误返回目前只由源码级检查、freestanding 语法检查与 cross-toolchain 构建覆盖；`BIGOS_SYSCALL_SMOKE_PASSED` 的实际运行时观测尚未在可稳定的 Bochs/serial 环境确认，属剩余 bootability 风险。
- **无用户指针校验**：本阶段从内核态触发、`SYS_DEBUG_WRITE` buffer 为内核内 bounded 来源，未做指针/长度校验。引入 ring3 后必须对用户态传入的指针与长度做范围校验与 bounded 拷贝，已在 `include/bigos/syscall.h`、`src/kernel/syscall/syscall.cc` 与 `docs/en/arch/syscall-entry.md` 记录为前置项。
- **DPL=0 门不允许 ring3 触发**：本阶段刻意保持 IDT gate DPL=0，`int 0x80` 仅能从 ring0 触发。允许 ring3 触发需把该 vector gate DPL 提升到 3，属后续 ring3 change 范围。
- **入口机制可能迁移到 `syscall`/`sysret`**：本阶段选 `int 0x80`；ABI 已与入口机制解耦（dispatcher 以 `InterruptFrame` 为输入），未来迁移需评估 `IA32_STAR/LSTAR/FMASK` MSR、段排列、TSS/`swapgs` 与内核栈切换成本。
- **boot_debug oracle 扩展**：观测 `BIGOS_SYSCALL_*` marker 需扩展 `tools/boot_debug.py` 注入 `syscall_smoke` 开关，记录为后续独立横切工程化项，未混入本 change。
