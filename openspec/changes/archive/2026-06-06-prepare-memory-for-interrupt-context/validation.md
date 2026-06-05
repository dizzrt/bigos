# Validation: prepare-memory-for-interrupt-context

## 已通过检查

- `uv run pytest tests/test_memory_interrupt_context_source.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py`
  - 结果：`24 passed`
  - 覆盖：allocator API 上下文标注、interrupt guard IF 保存/恢复 token、timer/keyboard/#PF 禁止普通 allocator token、`mm_self_test()` 在 IRQ/PIC/`sti` 前运行。
- `xmake`
  - 结果：通过。
  - 覆盖：默认配置下 kernel C++/assembly 目标构建。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/mm/buddy.cc src/mm/slab.cc src/mm/vmem.cc src/mm/kmem.cc cpp/libsupc++/new.cc src/kernel/irq/interrupt.cc`
  - 结果：通过。
  - 覆盖：新增/修改的 memory/IRQ guard 相关 C++ 源和头的 freestanding 语法检查。
- `openspec validate prepare-memory-for-interrupt-context --strict`
  - 结果：`Change 'prepare-memory-for-interrupt-context' is valid`。
- `uv run python tools/boot_debug.py run --image build/test/memory-interrupt-context.raw --serial-log build/test/memory-interrupt-context.serial.log --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED --smoke-timeout 30`
  - 结果：kernel build、boot build、image build 均完成；Bochs serial marker 等待超时。
  - 诊断：该结果与当前本机 Bochs/serial oracle 既有不稳定性一致，不能声明 runtime marker 通过。
- `xmake f --mm_self_test=n`
  - 结果：通过。
  - 用途：恢复默认关闭的 memory self-test 配置。

## 未运行或未通过检查

- Bochs memory self-test runtime marker 未通过：30 秒内未在 `build/test/memory-interrupt-context.serial.log` 观测到 `BIGOS_MM_SELF_TEST_PASSED`。
- 未继续运行普通 boot marker、`#PF` runtime marker 或人工 keyboard IRQ smoke：当前 serial oracle 已无法稳定观测 memory self-test marker，继续声明更多 runtime smoke 通过会产生误导。

## 历史诊断

- 既有 `docs/arch/interrupt-exception-foundation.md` 记录显示，本机普通 boot smoke、memory self-test runtime smoke 和 `#PF` runtime marker 曾受 Bochs/term GUI/serial 组合限制，无法作为可靠 oracle。
- 本次使用隔离 image `build/test/memory-interrupt-context.raw`，未遇到 image lock；失败点仍是 serial marker 超时。

## 当前 Change 影响

- 引入 `bigos::irq::InterruptGuard`，保存进入前 IF，进入时 `cli`，退出时仅按进入前 IF 状态恢复 `sti`。
- 为 public allocator、global `new/delete`、内部 buddy/vmem/slab helper 和诊断统计入口补充上下文契约。
- 在 buddy/slab/vmem 元数据和 accounting 更新边界增加单核 maskable IRQ guard，不改变 boot/linker 地址、direct map、`KVMEM_BASE` heap/vmalloc 语义、BootInfo handoff ABI、IDT vector 或 `InterruptFrame` ABI。
- 保持普通 `kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()` 和 global `new/delete` 为 non-IRQ-handler-safe；阶段 3 不承诺 IRQ handler 动态分配、SMP safety 或阻塞分配。

## 剩余风险

- 源码级检查和交叉构建覆盖了上下文契约与编译可用性，但 bootability 仍需在可稳定观测 serial/VGA 的 Bochs 环境中复核。
- `InterruptGuard` 仅面向单核 same-CPU maskable IRQ interleaving；后续 scheduler/SMP change 仍需引入明确的 preemption、spinlock 和 sleepable allocation 语义。
