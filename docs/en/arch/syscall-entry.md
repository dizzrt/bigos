# System Call Entry

BigOS stage 6 uses a controlled software-triggered kernel entry path and a minimal syscall ABI. The stage 5 ring0 diagnostic syscall remains available. The default-off `user_program_smoke` path configures GDT/TSS and a user address space, then allows CPL3 to enter the same dispatcher through `int 0x80`.

## Entry Mechanism: `int 0x80` Software Interrupt Gate

This stage uses an `int 0x80` software interrupt gate rather than the `syscall`/`sysret` fast-syscall path:

- It reuses the existing kernel-owned static IDT plus `interrupt.s` `isr_common` plus `irq_dispatch` framework. The `isr_entry` stub and dispatch framework for vector `0x80` already exist, so the entry is almost a zero-new-assembly path: `irq_dispatch` only needs to identify the syscall vector and route to the syscall dispatcher.
- Trade-off: `syscall`/`sysret` would require configuring `IA32_STAR/LSTAR/FMASK` MSRs, defining kernel/user segment ordering constraints, and preparing `swapgs`/kernel-stack policy. This change keeps the more explainable interrupt-gate + TSS/RSP0 path.
- DPL: only the `VECTOR_SYSCALL` gate is configured with DPL=3. Other CPU exception and i8259 IRQ gates remain ring0-only. Syscall is not an external IRQ, and the dispatch path does not send i8259 EOI.
- The vector is fixed by the named constant `VECTOR_SYSCALL = 0x80` declared centrally in `include/irq/interrupt.h`, avoiding scattered magic numbers.

## Minimal Syscall ABI

The mapping of syscall number, arguments, return value, and `InterruptFrame` fields is declared in `include/bigos/syscall.h` and asserted by source-level checks:

| Role | Register | `InterruptFrame` field |
| --- | --- | --- |
| syscall number | `rax` | `InterruptFrame.rax` |
| argument 0 | `rdi` | `InterruptFrame.rdi` |
| argument 1 | `rsi` | `InterruptFrame.rsi` |
| argument 2 | `rdx` | `InterruptFrame.rdx` |
| argument 3 | `r10` | `InterruptFrame.r10` |
| argument 4 | `r8` | `InterruptFrame.r8` |
| argument 5 | `r9` | `InterruptFrame.r9` |
| return value | `rax` | dispatcher writes `InterruptFrame.rax` |

- The syscall number is passed in `rax`; the return value is written back through `rax`, meaning the dispatcher writes `InterruptFrame.rax` and the caller reads `rax` after `iretq` returns.
- The fourth argument uses `r10` rather than `rcx`, following the SysV/Linux x86_64 syscall convention and avoiding the `rcx` clobber semantics associated with `int 0x80` / `iretq`.
- Registers other than the return value are caller-clobbered by convention; callers save what they need.
- The ABI is decoupled from the specific entry mechanism. The dispatcher consumes `InterruptFrame`; a future `syscall`/`sysret` implementation should only need a replacement entry stub while reusing the ABI and dispatch layer.

## Dispatch And Unknown Numbers

`bigos::sys::dispatch(InterruptFrame*)`:

- Reads the number from `InterruptFrame.rax` and routes through a bounded switch.
- Calls the corresponding implementation for known numbers and writes the result back through `rax`.
- Writes deterministic negative error `SYS_ENOSYS = -38` (equivalent to `-ENOSYS`) to `rax` for unknown numbers without crashing or entering the CPU exception path.
- `irq_dispatch` identifies syscall with `is_syscall_vector(vector == VECTOR_SYSCALL)`, calls `bigos::sys::dispatch`, and returns directly. This path **MUST NOT** send i8259 EOI because syscall is not an external IRQ. EOI semantics for CPU exceptions, external IRQs, and syscalls remain separate.

## Diagnostic Syscalls

- `SYS_DEBUG_WRITE` (number=0): writes a fixed/bounded in-kernel buffer through existing serial/console output with deterministic marker `BIGOS_SYSCALL_WRITE`, and returns the byte count. In this stage the caller is kernel-mode and the buffer is a bounded kernel source, so **no user pointer validation is performed**.
  - **Ring3 prerequisite**: once ring3 passes user buffer pointers and lengths, they **must** be validated against user address-space ranges and copied through a bounded path before output.
- `SYS_GET_TICK` (number=1): returns `bigos::timer::ticks()` monotonic tick to validate the return-register path. `timer::ticks()` is stably exposed by `include/bigos/timer.h` and is a context-agnostic bounded read, so it is used instead of `SYS_DEBUG_NOOP`.
- `SYS_WRITE` (number=2): supports only the early console sink (currently fixed `fd=1`). Before reading the user buffer, it checks low-half range, page-table present/user bits, and maximum length `SYS_WRITE_MAX_LEN`; then it writes bounded content to serial/VGA and returns a deterministic byte count or `SYS_EFAULT`.
- `SYS_EXIT` (number=3): records the current user process exit code, marks it terminated, restores the kernel address space, and enters the scheduler's deferred-reclamation exit path. This syscall does not return to terminated user instructions.

The syscall dispatcher follows the stage 3 interrupt-context contract: `int 0x80` context performs only bounded output/reads, current process state updates, and CR3 restoration. It does not dynamically allocate, and it does not call non-IRQ-safe allocators (`kmalloc`/`free`/`alloc_kernel_pages`/`free_pages`/global `new/delete`).

## Validation: Default-Off Build Switches And Deterministic Markers

The default-off xmake option `syscall_smoke` (`xmake f --syscall_smoke=y`) continues to validate `SYS_DEBUG_WRITE`, `SYS_GET_TICK`, and unknown numbers from ring0. The additional default-off `user_program_smoke` creates the first user process, calls `SYS_WRITE` / `SYS_EXIT` from CPL3, and emits `BIGOS_USER_*` markers. Default boot does not create a user process.

## Non-Goals For This Stage

- Do not switch to the `syscall`/`sysret` MSR fast path.
- Do not implement fork/exec/wait, signals, user threads, or a full file-descriptor table.
- Do not implement a complete syscall table or POSIX semantics; do not implement demand paging / COW. `#PF` remains diagnostic-only.
- Do not relax DPL for IDT gates other than syscall; do not send i8259 EOI from the syscall path.

## Cross-Cutting Engineering Items

This change did not modify `tools/boot_debug.py`. If later work needs it to inject `syscall_smoke` automatically and observe `BIGOS_SYSCALL_*` markers, that should be a separate cross-cutting engineering item rather than mixed into this change unless task scope is explicitly extended.
