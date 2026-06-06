# Validation

## 已通过检查

- `uv run pytest tests/test_kernel_thread_scheduler_source.py tests/test_memory_interrupt_context_source.py tests/test_timer_irq_foundation_source.py tests/test_interrupt_foundation_source.py`：36 passed。覆盖 TCB/context API 标注、1 页默认线程栈、terminated 延后回收、create 失败路径 allocator 契约、context switch symbol 与 callee-saved 集合、cooperative switch 不触碰 `InterruptFrame`/`iretq` ABI、round-robin yield、idle 线程替换裸 `hlt` loop、kernel 初始化顺序、timer bounded intent 无抢占、IRQ allocator 禁止和 scheduler smoke marker wiring。
- `xmake`（默认配置）：构建通过，新增 `src/kernel/sched/switch.s`、`src/kernel/sched/sched.cc` 已被现有 `src/kernel/**.cc` / `src/kernel/**.s` glob 自动编译链接。
- `xmake f --scheduler_smoke=y && xmake`：scheduler smoke 显式配置构建通过；随后 `xmake f -c` 恢复默认配置（smoke 默认关闭）。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -mno-red-zone -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/kernel/sched/sched.cc src/kernel/kernel.cc src/kernel/irq/isr.cc`：freestanding 语法检查通过，无告警。
- IDE diagnostics：当前修改/新增文件未报告诊断。
- `openspec validate introduce-kernel-threads-scheduler --strict`：通过（Change is valid）。

## 未运行或未通过检查

- Bochs runtime scheduler smoke 未通过：
  - `uv run python tools/boot_debug.py run --serial-log build/test/serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 40`：kernel 与 boot image 构建通过，但 40 秒内未观测到 `BigOS kernel reached` marker，且 `build/test/serial.log` 未生成。
  - 未尝试 scheduler smoke marker（`BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B`）的 Bochs runtime 观测：`tools/boot_debug.py` 的 `build_kernel` 只切换 `mm_self_test`，不支持注入 `scheduler_smoke` 构建开关。按 tasks 6.4，本 change 不把 `tools/boot_debug.py` 的 Python 修改混入；该 oracle 扩展记录为后续横切工程化项。

## 历史诊断

- Bochs serial oracle 在本仓库历史 change 中已多次不稳定。归档记录 `openspec/changes/archive/2026-06-06-establish-tty-console-input/validation.md` 同样记录构建通过但 Bochs serial smoke 在 30 秒内未观测到 `BigOS kernel reached`，与本次现象一致。因此本次 Bochs 超时被视为既有 oracle 限制，而非本 change 引入的回归。

## 当前 Change 影响

- 新增 public header：`include/bigos/thread.h`、`include/bigos/sched.h`。
- 新增调度器实现：`src/kernel/sched/sched.cc`、`src/kernel/sched/switch.s`。
- 修改 `src/kernel/kernel.cc`：保持 memory/self-test/TTY/IRQ 初始化顺序不变，在 `irq::enableIRQ()` 后进入 `sched::start()`，裸 `hlt` loop 由 idle 线程取代。
- 修改 `src/kernel/irq/isr.cc`：timer IRQ0 在 `on_tick()` 后调用 bounded `sched::on_timer_tick()`，不做抢占/分配。
- 修改 `xmake.lua`：新增默认关闭的 `scheduler_smoke` 开关。
- 新增文档/测试：`docs/en/arch/kernel-thread-scheduler.md`、`tmp/roadmap.md`、`tests/test_kernel_thread_scheduler_source.py`。
- 未改动 boot 固定地址、higher-half/load base、BootInfo ABI、direct map、`KVMEM_BASE`、IDT vector 分配或 `InterruptFrame` layout；现有 timer/interrupt 源码级测试保持通过。

## 剩余风险

- **Scheduler runtime 未经 oracle 验证**：cooperative context switch、trampoline 栈帧、yield/idle 路径目前只由源码级检查与 freestanding 构建/语法检查覆盖；两个线程交替 marker 的实际运行时行为尚未在可稳定观测的 Bochs/serial 环境中确认，属剩余 bootability/scheduler runtime 风险。
- **Terminated 线程延后回收**：阶段 4 不释放当前线程 TCB/栈，安全回收留给后续 lifecycle change；阶段 4 线程数量 bounded。
- **1 页线程栈无 guard page**：复杂内核路径栈溢出未做诊断，后续需单独 change 扩大栈或加 guard page。
- **无 IRQ 返回前抢占**：timer 只记录 bounded reschedule intent，抢占切换留给后续专门 change 设计与验证。
