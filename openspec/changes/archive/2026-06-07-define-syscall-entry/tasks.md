## 1. syscall 入口常量与 ABI 声明

- [x] 1.1 在 `include/irq/interrupt.h`（或等价中断头）新增 `VECTOR_SYSCALL = 0x80` 具名常量，集中声明，避免散落魔数。
- [x] 1.2 在 `bigos`（建议 `bigos::sys`）公共头中声明 syscall number 枚举（`SYS_DEBUG_WRITE` 及 `SYS_DEBUG_NOOP`/`SYS_GET_TICK`）、统一错误码（等价 `-ENOSYS`）与 dispatch 入口声明，保持公共头最小化。
- [x] 1.3 文档化最小 syscall ABI：number=`rax`、参数=`rdi/rsi/rdx/r10/r8/r9`、返回值=`rax`，及其与 `InterruptFrame` 字段的对应关系。

## 2. dispatch 路由与 syscall 实现

- [x] 2.1 在 `irq_dispatch` 中新增 `is_syscall_vector`（vector==`VECTOR_SYSCALL`）分支，路由到 `bigos::sys::dispatch(InterruptFrame*)`；确认该分支 MUST NOT 发送 i8259 EOI，且不改动 exception/外部 IRQ 既有分支与 EOI 语义。
- [x] 2.2 实现 `bigos::sys::dispatch`：从 `InterruptFrame.rax` 读取 number，用 bounded switch/跳转表路由；未知 number 在 `InterruptFrame.rax` 写入确定性负错误码并安全返回，不崩溃、不进入异常路径。
- [x] 2.3 实现 `SYS_DEBUG_WRITE`：把内核内 bounded buffer 经现有 console/串口输出确定性 `BIGOS_SYSCALL_*` marker；本阶段不校验指针，但把 buffer 限制为内核内 bounded 来源，并在代码/文档记录“ring3 后必须加用户指针/长度校验”。
- [x] 2.4 实现第二个诊断 syscall（`SYS_GET_TICK` 返回单调 tick，或 `SYS_DEBUG_NOOP` 返回固定值），验证返回值寄存器路径；按 `timer::ticks()` 是否稳定暴露二选一并记录。
- [x] 2.5 确认 dispatch 与诊断 syscall 遵守kernel memory API capability 中断上下文契约：只做 bounded 输出/读取，不在该路径调用 non-IRQ-safe allocator、不做动态分配。

## 3. 边界与非目标固定

- [x] 3.1 用源码级检查固定本阶段不进入 ring3、不切换 CR3、不加载用户 ELF、`#PF` 保持诊断-only。
- [x] 3.2 用源码级检查固定本阶段不修改 IDT gate 的 DPL、不引入 GDT user/TSS 条目或 `syscall`/`sysret` MSR 配置；在 design/文档记录“提升该 vector DPL 到 3 属于后续 ring3 change 范围”。

## 4. Smoke 与文档

- [x] 4.1 在 `xmake.lua` 新增默认关闭的 `syscall_smoke` 构建开关，在 `kernel()` 非中断上下文从内核态发起一次 `int 0x80` 自测（设置 `rax`+参数寄存器、执行 `int $0x80`、读取返回 `rax`），断言 dispatcher 命中、返回值正确、未知 number 返回错误码，并输出确定性 `BIGOS_SYSCALL_*` marker（成功/失败）；默认 boot 行为不变。
- [x] 4.2 更新 `docs/en/arch` 中中断/入口设计说明，记录 syscall 入口机制选择（`int 0x80` 及取舍）、最小 ABI、dispatch 与未知 number 处理、诊断 syscall，以及本阶段非目标（不进 ring3、不切 CR3、无完整 syscall 表）。
- [x] 4.3 若需要调整 `tools/boot_debug.py` 才能注入 `syscall_smoke` 开关并观测 marker，单独记录为横切工程化项，不把 Python 修改混入本 change，除非明确扩展任务范围。

## 5. Validation

- [x] 5.1 新增 `tests/test_syscall_entry_source.py`，覆盖：`VECTOR_SYSCALL` 常量与 ABI 声明、`irq_dispatch` 识别 syscall vector 且不发送 EOI、number/参数/返回值寄存器约定、已知 number 路由到实现、未知 number 返回确定性错误码、诊断 syscall 输出 marker / 返回预期值、本阶段不进 ring3 / 不切 CR3 / 不改 DPL、smoke marker wiring。
- [x] 5.2 运行 `uv run pytest tests/test_syscall_entry_source.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py` 及相关源码级测试，记录结果；若 `uv` 不可用显式记录 blocker，不静默回退系统 Python。
- [x] 5.3 运行默认 `xmake`，并在需要时运行 `syscall_smoke=y` 构建，记录 cross-toolchain 构建结果；随后 `xmake f -c` 恢复默认配置。
- [x] 5.4 对新增/修改 C++ 源和头运行贴近 GCC cross-build 的 freestanding `x86_64-elf-g++ -fsyntax-only`，并按需用 clang/clangd 辅助诊断（freestanding C++17、x86_64 target、project include、无 hosted runtime、无异常、无 RTTI）；修复本 change 引入的诊断，区分历史诊断、本 change 诊断与工具链/freestanding 误报；若工具缺失记录 blocker 与剩余风险。
- [x] 5.5 复核中断 ABI 安全：确认 syscall 分支沿用既有 `isr_common` 栈对齐与寄存器保存、`iretq` 返回路径不变，syscall 路径不调用 `send_eoi`；若 Bochs/serial oracle 不可用，记录命令、失败点、历史 oracle 状态与剩余 bootability 风险。
- [x] 5.6 运行 `openspec validate define-syscall-entry --strict`，并在 `validation.md` 中分开记录已通过检查、未运行或未通过检查、历史诊断、当前 change 影响和剩余风险。
