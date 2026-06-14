# System Call Entry

BigOS stage 6 uses a controlled software-triggered kernel entry path and a minimal syscall ABI. The stage 5 ring0 diagnostic syscall remains available. The default-off `user_program_smoke` path configures GDT/TSS and a user address space, then allows CPL3 to enter the same dispatcher through `int 0x80`.

## Entry Mechanism: `int 0x80` Software Interrupt Gate

This stage uses an `int 0x80` software gate rather than the `syscall`/`sysret` fast-syscall path:

- It reuses the existing kernel-owned static IDT plus `interrupt.s` `isr_common` plus `irq_dispatch` framework. The `isr_entry` stub and dispatch framework for vector `0x80` already exist, so the entry is almost a zero-new-assembly path: `irq_dispatch` only needs to identify the syscall vector and route to the syscall dispatcher.
- Trade-off: `syscall`/`sysret` would require configuring `IA32_STAR/LSTAR/FMASK` MSRs, defining kernel/user segment ordering constraints, and preparing `swapgs`/kernel-stack policy. This change keeps the more explainable interrupt-gate + TSS/RSP0 path.
- DPL: only the `VECTOR_SYSCALL` gate is configured with DPL=3. It is a trap gate so ordinary process syscalls preserve IF and fd/VFS syscalls can pass `sched::can_block()`. Other CPU exception and i8259 IRQ gates remain ring0-only interrupt gates. Syscall is not an external IRQ, and the dispatch path does not send i8259 EOI.
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
- Writes deterministic negative error `-bigos::ENOSYS` (value `-38`) to `rax` for unknown numbers without crashing or entering the CPU exception path. The POSIX-style error codes are centralized in `include/bigos/errno.h` as positive values and negated when written to the return register.
- `irq_dispatch` identifies syscall with `is_syscall_vector(vector == VECTOR_SYSCALL)`, calls `bigos::sys::dispatch`, and returns directly. This path **MUST NOT** send i8259 EOI because syscall is not an external IRQ. EOI semantics for CPU exceptions, external IRQs, and syscalls remain separate.

## Diagnostic Syscalls

- `SYS_DEBUG_WRITE` (number=0): writes a fixed/bounded in-kernel buffer through existing serial/console output with deterministic marker `BIGOS_SYSCALL_WRITE`, and returns the byte count. In this stage the caller is kernel-mode and the buffer is a bounded kernel source, so **no user pointer validation is performed**.
  - **Ring3 prerequisite**: once ring3 passes user buffer pointers and lengths, they **must** be validated against user address-space ranges and copied through a bounded path before output.
- `SYS_GET_TICK` (number=1): returns `bigos::timer::ticks()` monotonic tick to validate the return-register path. `timer::ticks()` is stably exposed by `include/bigos/timer.h` and is a context-agnostic bounded read, so it is used instead of `SYS_DEBUG_NOOP`.
- `SYS_WRITE` (number=2): supports only the early console sink (currently fixed `fd=1`). Before reading the user buffer, it checks low-half range, page-table present/user bits, and maximum length `SYS_WRITE_MAX_LEN`; then it writes bounded content to serial/VGA and returns a deterministic byte count or `-bigos::EFAULT`.
- `SYS_EXIT` (number=3): records the current user process exit code, marks it terminated, restores the kernel address space, and enters the scheduler's deferred-reclamation exit path. This syscall does not return to terminated user instructions.
- `SYS_WAIT` (number=4): waits for child process state when the caller can block, optionally copies the bounded raw exit status to a user `int*`, or returns the deterministic wait error for unsupported/nonblocking contexts.
- `SYS_OPEN` (number=5): copies a bounded NUL-terminated user path, accepts only read-only flags, opens through the VFS shell, and returns a process-local fd.
- `SYS_READ` (number=6): validates the user destination range, reads through the process fd table and VFS file offset into a bounded kernel buffer, copies out, and returns the byte count.
- `SYS_CLOSE` (number=7): closes the process-local fd and drops the open-file reference.

`SYS_BRK` (8), `SYS_MAP_ANON` (9), and `SYS_FORK` (10) follow. The read-only
identity/time queries are appended after them and never block, allocate, or send
an EOI:

- `SYS_GET_TIME` (number=11): returns the current wall-clock time in Unix epoch
  seconds (`bigos::time::current_unix_time()`); see
  `docs/en/arch/wall-clock-and-identity.md`.
- `SYS_GETPID` (number=12) / `SYS_GETPPID` (number=13): return the current
  process `pid` / `parent_pid`.
- `SYS_GETUID` (number=14) / `SYS_GETGID` (number=15): return the current process
  `uid` / `gid`.

`SYS_KILL` (16), `SYS_SIGACTION` (17), `SYS_SIGPROCMASK` (18), `SYS_SIGRETURN`
(19), `SYS_LSEEK` (20), `SYS_PIPE` (21), `SYS_DUP` (22), `SYS_DUP2` (23),
`SYS_FSYNC` (24), `SYS_MKDIR` (25), and `SYS_UNLINK` (26) follow.

- `SYS_EXECVE` (number=27): append-only exposure of the existing in-kernel image
  replacement path (`exec_current_from_elf_image` + the read-only VFS read path)
  to CPL3. ABI: `rdi` = user `path`, `rsi` = `argv` (NULL-terminated user pointer
  array), `rdx` = `envp`. The path is bounded by `SYS_PATH_MAX_LEN` and the
  argv/envp vectors by `EXEC_MAX_ARGC` / `EXEC_MAX_ENVC` / `EXEC_MAX_STRING_BYTES`;
  all user buffers are copied through the VMA-backed validation path before use.
  On success it replaces the current process address space with the new ELF64
  `ET_EXEC` image and enters the new program entry, so it does not return to the
  caller. On failure it returns a deterministic negative errno (`-ENOENT`,
  `-EACCES`, `-ENOEXEC`, `-E2BIG`, `-EFAULT`, `-ENOMEM`, `-EWOULDBLOCK`, `-EIO`)
  with the calling image left able to continue. Like the other fd/VFS syscalls it
  checks the `sched::can_block()` guard before allocation or synchronous storage
  IO, and it does not change any existing syscall number, register convention,
  `VECTOR_SYSCALL` / DPL layout, or the "syscall sends no EOI" rule.
- `SYS_READDIR` (number=28): reads a bounded batch of directory entries from an
  open fd into a user `struct bigos_dirent[]`. ABI: `rdi` = fd, `rsi` = user
  entries buffer, `rdx` = requested entry count. The request is bounded by
  `SYS_DIRENT_MAX_ENTRIES`, copies out through user-buffer validation, and
  returns an entry count or a deterministic negative fd/VFS errno.
- `SYS_STAT` (number=29) / `SYS_FSTAT` (number=30): copy bounded metadata into a
  user `struct stat`. `SYS_STAT` takes `rdi` = user path and `rsi` = user output
  pointer; `SYS_FSTAT` takes `rdi` = fd and `rsi` = user output pointer. These
  expose the current file-vs-directory metadata subset only, not complete POSIX
  `stat(2)` semantics.
- `SYS_CHDIR` (number=31): takes `rdi` = user path and commits the process cwd
  only after bounded path copy, resolution, and directory validation.
- `SYS_GETCWD` (number=32): takes `rdi` = user buffer and `rsi` = buffer size,
  then copies the NUL-terminated cwd or returns deterministic `-ERANGE`,
  `-EFAULT`, or `-EINVAL`.
- `SYS_RENAME` (number=33): takes `rdi` = old user path and `rsi` = new user path.
  It is restricted to the current bounded writable `/rw` regular-file semantics
  and does not imply full persistent filesystem or cross-device rename support.

The syscall dispatcher keeps exception/IRQ/syscall EOI separation unchanged. CPU exceptions and external IRQs remain nonblocking contexts. fd/VFS syscalls check `sched::can_block()` before allocation or synchronous ATA PIO/exFAT reads; ordinary user process syscalls can pass that guard because the DPL=3 trap gate preserves IF.

Userland raw syscall primitives `syscall0` through `syscall6` remain
BigOS-specific low-level helpers. They bind the number and return value to `rax`,
arguments to `rdi`, `rsi`, `rdx`, `r10`, `r8`, and `r9`, and list `rcx`, `r11`,
and `memory` clobbers. A source-level contract test checks these constraints so
wrapper edits cannot silently drift from this ABI; higher-level libc wrappers
remain responsible for translating negative kernel returns into positive
`errno` plus the documented failure sentinel.

## Validation: Default-Off Build Switches And Deterministic Markers

The default-off xmake option `syscall_smoke` (`xmake f --syscall_smoke=y`) continues to validate `SYS_DEBUG_WRITE`, `SYS_GET_TICK`, and unknown numbers from ring0. Additional default-off smokes cover the flat first user program, filesystem-backed user ELF, demand paging, fork/COW, time/identity, signals, writable FS, pipes, and userland runtime. Normal boot now packages `/boot/user/init.elf`, enters resident PID-1 init, and starts `/bin/sh`; default headless validation observes `BIGOS_USER_EXEC`.

## Non-Goals For This Stage

- Do not switch to the `syscall`/`sysret` MSR fast path.
- Do not reinterpret the bounded syscall set as complete POSIX-wide syscall semantics, user threads, job control, dynamic linking, or a full libc.
- Do not broaden demand paging/COW beyond the current bounded anonymous mappings or add broad file-backed `mmap`.
- Do not relax DPL for IDT gates other than syscall; do not send i8259 EOI from the syscall path.

## Cross-Cutting Engineering Items

This change did not modify `tools/boot_debug.py`. If later work needs it to inject `syscall_smoke` automatically and observe `BIGOS_SYSCALL_*` markers, that should be a separate cross-cutting engineering item rather than mixed into this change unless task scope is explicitly extended.
