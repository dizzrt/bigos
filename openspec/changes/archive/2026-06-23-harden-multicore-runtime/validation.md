## Validation

### Build And Source Checks

- `xmake`
  - Result: passed with the default smoke configuration after restoring `--multicore_hardening_smoke=n`.
- `xmake f --multicore_hardening_smoke=y`
  - Result: passed.
- `xmake` with `--multicore_hardening_smoke=y`
  - Result: passed.
- `uv run ruff check tools/boot_debug.py`
  - Result: passed.
- `uv run ruff format --check tools/boot_debug.py`
  - Result: passed after formatting the edited helper file with `uv run ruff format tools/boot_debug.py`.
- `uv run pyright tools/boot_debug.py`
  - Result: passed. Tool emitted only the local pyright update notice.
- `uv run pytest`
  - Result: failed with 20 pre-existing source-string and archived-OpenSpec assertions unrelated to this change.
  - Notable unrelated failures include missing `openspec/changes/add-metadata-consistency/proposal.md`, archived validation text containing old `src/kernel`, and historical assertions for VM/proc/syscall source strings that no longer match the current code.
- `openspec status --change "harden-multicore-runtime" --json`
  - Result: passed.
- `openspec validate harden-multicore-runtime --strict`
  - Result: passed.

### C++ Auxiliary Checks

- `clangd --check=kernel/core/kernel.cc --compile-commands-dir=.`
  - Result: passed with `All checks completed, 0 errors`.
- `clangd --check=kernel/core/sched/sched.cc --compile-commands-dir=.`
  - Result: blocked by clangd check-mode tweak diagnostics (`AddUsing` / `ExtractFunction` failures), not by x86_64 cross-build compilation errors.
- `clangd --check=kernel/mm/vmem.cc --compile-commands-dir=.`
  - Result: blocked by clangd check-mode tweak diagnostics (`SwapBinaryOperands` / `ExtractFunction` replacement failures), not by x86_64 cross-build compilation errors.
- `clangd --check=kernel/core/bigos/ap_startup.cc --compile-commands-dir=.`
  - Result: blocked by clangd check-mode tweak diagnostics (`ExtractFunction` failures), not by x86_64 cross-build compilation errors.

### Runtime Smoke

- `uv run python tools/boot_debug.py runtime-smoke-matrix --case multicore-hardening`
  - Result: passed.
  - Emulator: QEMU headless.
  - Boot mode: legacy.
  - CPU count: 2 (`-cpu max -smp 2`).
  - Timeout: 25s.
  - Serial log: `log/runtime-smoke/multicore-hardening.serial.log`.
  - Observed markers: `BIGOS_AP_ONLINE`, `BIGOS_AP_LOCAL_TIMER`, `BIGOS_MULTICORE_HARDENING_AP_THREAD`, `BIGOS_MULTICORE_HARDENING_IPI`, `BIGOS_MULTICORE_HARDENING_REMOTE_WAKE`, `BIGOS_MULTICORE_HARDENING_TIMEOUT_WAKE`, `BIGOS_MULTICORE_HARDENING_TLB`, `BIGOS_MULTICORE_HARDENING_PASSED`, `BIGOS_USER_EXEC`.
- Earlier QEMU UEFI attempt:
  - Result: failed to observe the pass marker because the current UEFI path reported `BIGOS_ACPI_RSDP_MISSING`, fell back to `BIGOS_APIC_DEFAULT_BSP_ONLY_FALLBACK`, and the hardening smoke emitted `BIGOS_MULTICORE_HARDENING_SKIPPED cpu`.
  - Interpretation: not counted as multi-core APIC-backed runtime coverage; the passing runtime coverage is the legacy MP-table QEMU path above.
- `uv run python tools/boot_debug.py run --boot-mode legacy --emulator bochs --display none --serial-log log/runtime-smoke/multicore-hardening.bochs.serial.log --expect-serial-marker BIGOS_MULTICORE_HARDENING_PASSED --smoke-timeout 25 --bochs-cpus 2`
  - Result: passed.
  - Emulator: Bochs headless.
  - CPU count: 2.
  - Serial log: `log/runtime-smoke/multicore-hardening.bochs.serial.log`.
  - Note: Bochs serial output interleaved some concurrent AP/BSP marker bytes, but the helper observed `BIGOS_MULTICORE_HARDENING_PASSED`.

### Covered Behavior

- AP startup and local timer readiness before AP scheduler participation.
- Scheduler remote enqueue / wait queue wakeup with wait-before-release lock ordering.
- Timeout wakeup from the AP local timer path.
- Scheduler nudge typed IPI delivery kept separate from TLB shootdown.
- TLB shootdown remote target acknowledgement and fail-closed diagnostics.
- Baseline userland marker `BIGOS_USER_EXEC` after the default init path.

### Skipped Or Residual Risk

- UEFI APIC-backed multi-core runtime coverage remains unavailable in this local run because the current UEFI loader path did not provide ACPI RSDP/MADT to the kernel.
- No CPU hotplug, NUMA, async I/O, complete POSIX concurrency model, broad IRQ affinity, MSI/MSI-X, or UEFI runtime parity is claimed.
