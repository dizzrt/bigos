# Validation Notes

## 通过检查

- `openspec validate upgrade-scheduler-semantics --strict`：通过。
- `GetDiagnostics`：无 VS Code/clangd 诊断。
- `xmake f --scheduler_semantics_smoke=y && xmake`：通过；链接器保留既有 `LOAD segment with RWX permissions` warning。
- `uv run python tools/boot_debug.py runtime-smoke-matrix --case scheduler-semantics --output build/test/runtime-smoke-validation.md`：通过，QEMU headless 观察到 `BIGOS_SCHED_SEMANTICS_PASSED`。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/runtime-smoke/scheduler-semantics.serial.log --expect-serial-marker BIGOS_SCHED_SEMANTICS_PASSED --smoke-timeout 15`：通过。
- `uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/runtime-smoke/scheduler-semantics.bochs.serial.log --expect-serial-marker BIGOS_SCHED_SEMANTICS_PASSED --smoke-timeout 15`：通过。
- `uv run ruff check tools/boot_debug.py`：通过。
- `uv run ruff format --check tools/boot_debug.py`：通过。
- `uv run pyright`：通过；输出提示 `bigos.py` 不存在以及 pyright 新版本提醒，未产生 error/warning/information。
- `uv run pytest tests/test_kernel_thread_scheduler_source.py::test_timer_irq_records_bounded_intent_without_preemption tests/test_timer_irq_foundation_source.py::test_timer_handler_does_not_send_pic_eoi_or_allocate`：通过。

## 失败或历史问题

- `uv run ruff check`：失败在既有测试文件格式问题，涉及 `tests/test_address_space_lifecycle_source.py`、`tests/test_bilingual_docs_layout.py`、`tests/test_first_user_program_source.py`、`tests/test_memory_correctness_source.py`；本 change 未修改这些文件。
- `uv run ruff format --check`：失败在既有测试文件格式问题，涉及 `tests/test_address_space_lifecycle_source.py`、`tests/test_first_user_program_source.py`、`tests/test_memory_correctness_source.py`、`tests/test_memory_interrupt_context_source.py`；本 change 未修改这些文件。
- `uv run pytest`：145 项中 142 通过；修复本 change 相关的 scheduler/timer source-check 后，剩余失败为 `tests/test_user_address_space_vmem_source.py::test_derivation_does_not_switch_cr3_and_runtime_activation_is_explicit`，断言 `src/kernel/proc/proc.cc` 中存在特定 `activate_address_space_root` 调用字符串；该失败与 scheduler/timer/IRQ-return preemption 改动无关。

## Runtime Artifacts

- QEMU matrix artifact：`build/test/runtime-smoke-validation.md`。
- QEMU scheduler semantics serial log：`build/test/runtime-smoke/scheduler-semantics.serial.log`。
- Bochs scheduler semantics serial log：`build/test/runtime-smoke/scheduler-semantics.bochs.serial.log`。
- 观察到的 scheduler semantics markers：`BIGOS_SCHED_SEMANTICS_START`、`BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED`、`BIGOS_SCHED_SEMANTICS_PREEMPTED`、`BIGOS_SCHED_SEMANTICS_PASSED`。

## 残余风险

- IRQ-return bridge 仍依赖单核、kernel-mode interrupted frame、EOI 后调用顺序和 `switch_context` 保存当前内核栈 continuation 的约束；已通过 QEMU headless 与 Bochs headless smoke 交叉验证，但尚未覆盖 SMP、user-mode preemption、syscall sleepability 或完整 priority policy。
- `scheduler-semantics` smoke 只覆盖 bounded time slice 到期、preemption-disable 延迟和一个 timer-driven preemption pass；更复杂的 fairness、starvation、process lifecycle、fd/VFS 和 user CR3 场景仍属于后续 change。
