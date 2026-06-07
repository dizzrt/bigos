# Validation Record

## Source-Level Checks

- Passed: `uv run pytest tests/test_address_space_lifecycle_source.py tests/test_user_address_space_vmem_source.py tests/test_first_user_program_source.py tests/test_memory_correctness_source.py tests/test_bilingual_docs_layout.py` (`49 passed`).
- Passed: `uv run pytest` (`115 passed`).
- Coverage: page-table ownership metadata, publish-before-present ordering, rollback, present-entry accounting, empty PT/PD/PDPT reclaim, user teardown, active-root rejection, current-stack deferral, reaper handoff, existing user-vmem/process/memory/doc invariants.

## Build Checks

- Passed: toolchain discovery found `x86_64-elf-gcc`, `x86_64-elf-g++`, `xmake`, and `bochs`.
- Passed: `xmake` default build (`build ok`).
- Passed: `xmake f --user_program_smoke=y && xmake` (`build ok`), covering `src/kernel/proc/**`, syscall exit/fault, and address-space teardown wiring.

## Runtime Smoke

- Attempted: `uv run python tools/boot_debug.py run --serial-log build/test/user-reclaim-serial.log --expect-serial-marker BIGOS_USER_RECLAIMED`.
- Result: failed due timeout waiting for `BIGOS_USER_RECLAIMED`; `build/test/user-reclaim-serial.log` was not produced.
- Remaining risk: boot/runtime reclamation marker was not observed under local Bochs despite successful image/build steps; source-level checks and cross-builds are the current evidence.

## Static Diagnostics

- Passed: VS Code diagnostics for `src/mm/vmem.cc`, `src/kernel/proc/proc.cc`, `src/kernel/syscall/syscall.cc`, `src/kernel/irq/interrupt.cc`, and `src/kernel/sched/sched.cc`.
- Passed: `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -nostdinc++ -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/mm/vmem.cc src/kernel/proc/proc.cc src/kernel/syscall/syscall.cc src/kernel/irq/interrupt.cc src/kernel/sched/sched.cc -DBIGOS_USER_PROGRAM_SMOKE`.
- Note: `clangd` binary is present, but no `compile_commands.json` exists; editor diagnostics were used as the clangd auxiliary signal.

## OpenSpec

- Passed: `openspec validate reclaim-address-space-page-tables --strict`.
