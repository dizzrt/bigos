# Validation Notes

## Boundary Audit

- `kernel/mm/vmem.cc` 仍拥有 x86_64 page-table materialization、CR3 read/write、TLB flush 和 user-root teardown 机制。
- `kernel/core/proc/proc.cc` 仍拥有 VMA/process policy、`execve` rollback、demand-zero/COW/stack-growth recovery、safe fault/exit lifecycle 和 deferred reaper policy。
- `kernel/core/proc/arch_vm_user_boundary.cc` 是本 change 新增的窄 VM/user-entry boundary，集中 active-root query、address-space activation、TSS/user-entry setup、user-return classification、selector restore 和 fork resume-frame capture。
- `kernel/core/irq/interrupt.cc` 只通过 boundary 判断 CPL3/user-return frame；CPL0 fault 仍走原有 diagnostic/panic path。
- `kernel/core/syscall/syscall.cc` 的 console write 临时 root restore 不再 open-code CR3 mask 或 raw activation。

## Preserved ABI Assumptions

- 未改变 boot address、linker layout、higher-half kernel mapping、direct map、KVMEM、recursive/self-mapping、IDT/syscall vector、disk layout、user stack layout 或 syscall register ABI。
- 未改变 `InterruptFrame` layout、ISR save/restore order、`iretq` frame layout、GDT/TSS descriptor layout 或 x86_64 selector values。
- 未改变 CR3 switching semantics；raw CR3 read/write 仍在 `kernel/mm/vmem.cc`，核心用户态消费点通过 `include/bigos/arch_vm_user_boundary.h` 表达语义。
- 未扩大 page fault recovery 范围；CPL3 recovery 仍限于 VMA-backed demand-zero、COW split 和 stack-growth/anonymous materialization，其他 user fault 进入 process fault/exit path。
- 未改变 safe teardown/reaper 策略；active root 和当前 kernel stack 仍被检测并延迟释放。

## Remaining Low-Level Coupling

- `include/bigos/memory.h` 仍公开 `read_cr3()` 和 `activate_address_space_root()`，因为 `kernel/mm` 当前也是 x86_64 paging implementation owner；后续若需要更严格隐藏，可拆独立 change 收窄公开面。
- `include/irq/interrupt.h` 的 `InterruptFrame` 仍是 syscall、signal、fork 和 ISR assembly 的共同 ABI；本 change 只把普通 process/IRQ policy 中的 CPL/iret-tail/selector 消费收敛到 VM/user-entry boundary。
- `include/bigos/user_mode.h` 仍是 x86_64 低层 user-entry header；普通 process path 不再直接调用它，`arch_vm_user_boundary.cc` 作为当前唯一语义 wrapper。

## Validation Scope

- Source changes touch C++ headers/sources in VM/user-entry/fault boundaries, so validation requires OpenSpec checks, targeted consistency search, diagnostics, and the narrowest available cross-toolchain build.
- Runtime smoke is recommended because the change touches address-space activation, user-entry and fault-return-adjacent code. If local QEMU/Bochs/toolchain pieces are unavailable, record the unavailable dependency and remaining bootability/userland risk instead of claiming smoke coverage.

## Executed Checks

- `openspec status --change harden-vm-user-entry-boundary` passed: proposal/design/specs/tasks complete.
- `openspec validate harden-vm-user-entry-boundary --strict` passed.
- Targeted consistency search for direct CR3/root-mask/x86 selector/CPL checks in `kernel/core` now reports only `kernel/core/proc/arch_vm_user_boundary.cc` and the existing low-level `kernel/core/proc/user_mode.cc`.
- `GetDiagnostics` reported no IDE diagnostics after the source/header edits.
- `xmake` passed with the x86_64 cross-toolchain.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/vm-user-entry-boundary-serial.log --expect-serial-marker BIGOS_USER_EXEC` passed and observed `BIGOS_USER_EXEC`.
- `command -v bochs` found `/opt/homebrew/bin/bochs`; Bochs cross-validation was not run because this change did not modify early boot, page-table layout, port IO, or hardware behavior. Residual Bochs-only hardware risk is unchanged from the baseline.
