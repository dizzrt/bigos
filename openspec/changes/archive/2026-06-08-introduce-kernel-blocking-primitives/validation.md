## Validation Summary

- `openspec validate introduce-kernel-blocking-primitives --strict`: passed.
- `uv run pytest tests/test_kernel_thread_scheduler_source.py tests/test_timer_irq_foundation_source.py tests/test_tty_console_input_source.py tests/test_boot_debug.py`: passed, 62 tests.
- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py tests/test_kernel_thread_scheduler_source.py tests/test_tty_console_input_source.py`: passed.
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py tests/test_kernel_thread_scheduler_source.py tests/test_tty_console_input_source.py`: passed after formatting changed Python files.
- `uv run pyright`: passed with 0 errors; pyright also reported a pre-existing missing optional `bigos.py` path warning.
- `xmake f --blocking_smoke=y && xmake`: passed; linker preserved the existing RWX LOAD segment warning.
- `uv run python tools/boot_debug.py runtime-smoke-matrix --case blocking-primitives --output build/test/runtime-smoke-validation-blocking.md`: passed.

## Blocking Smoke Artifact

- Artifact: `build/test/runtime-smoke-validation-blocking.md`.
- Serial log: `build/test/runtime-smoke/blocking-primitives.serial.log`.
- Switches: `xmake f --blocking_smoke=y` with other smoke switches disabled by the matrix runner.
- Expected marker: `BIGOS_BLOCKING_SMOKE_PASSED`.
- Observed markers: `BIGOS_BLOCKING_WAIT_BLOCKED`, `BIGOS_BLOCKING_WAKE_SENT`, `BIGOS_BLOCKING_WAIT_RESUMED`, `BIGOS_BLOCKING_TIMEOUT_BLOCKED`, `BIGOS_BLOCKING_TIMEOUT_EXPIRED`, `BIGOS_BLOCKING_SMOKE_PASSED`.

## Source-Level Coverage

- Wait state definitions: `ThreadState::Blocked` and `ThreadState::Sleeping`.
- Intrusive wait/timeout nodes: `wait_next`, `wait_queue`, `sleep_next`, `deadline_tick`, `wait_result`.
- Wait queue enqueue/dequeue and wakeup idempotence: `wait_queue_wait_until()`, `wake_one()`, `wake_all()`, and `wake_thread_locked()`.
- Timeout removal: `sched::on_timer_tick()` removes expired sleepers from timeout tracking and wait queues before making them runnable.
- Forbidden blocking contexts: `sched::can_block()` checks scheduler state, current thread, nonblocking depth, scheduler critical depth, and interrupt enable state; `irq_dispatch()` installs a nonblocking guard for IRQ, exception, and syscall vectors.
- TTY producer boundary: `enqueue_input()` preserves fixed-capacity overflow policy and only calls allocation-free `sched::wake_one()` after successful enqueue.

## Skipped Or Blocked Checks

- Full `uv run pytest`: 144 passed, 1 failed in an unrelated pre-existing `tests/test_user_address_space_vmem_source.py` assertion against `src/kernel/proc/proc.cc`; this change did not edit that source path.
- Full `uv run ruff check`: blocked by pre-existing style drift in unrelated tests (`test_address_space_lifecycle_source.py`, `test_bilingual_docs_layout.py`, `test_first_user_program_source.py`, `test_memory_correctness_source.py`). Changed Python files pass scoped ruff checks.
- Full `uv run ruff format --check`: blocked by pre-existing unrelated formatting drift plus changed files before scoped formatting. Changed Python files pass scoped format check after formatting.
- Bochs or QEMU+Bochs cross-validation: not run in this session. QEMU headless marker smoke, source checks, OpenSpec validation, pyright, and cross build were used as substitutes. Residual risk remains for Bochs-specific timer/keyboard IRQ and port-IO behavior.
