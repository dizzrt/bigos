# First User Program Runtime Path

Stage 6 `user_program_smoke` is a default-off validation path that proves BigOS can create a minimal user process, load an embedded user program, enter CPL3, and return to the kernel through `write` / `exit` syscalls.

Stage 8 adds a separate default-off `user_elf_smoke` path. Stage 12 promotes the
shared process runtime into a normally compiled lifecycle core while keeping both
smoke entries default-off. The ELF smoke reuses the lifecycle core, syscall gate,
user fault path, bounded `argv`/`envp` stack setup, and deferred reaper, but loads
`/boot/user/init.elf` from the kernel read-only exFAT stack instead of the
embedded flat blob.

## Image Format

The first user program uses an embedded flat blob rather than ELF64 or filesystem loading:

- The flat blob is provided by the `FIRST_USER_CODE` byte sequence in `src/kernel/proc/proc.cc` and linked into the kernel image.
- The image does not depend on an in-kernel FS, block devices, hosted OS file IO, or bootloader-only exFAT helpers.
- The blob executes only a bounded `SYS_WRITE(fd=1, buf, len)` and then `SYS_EXIT(0)`.
- The loader still maps code, data/BSS, and stack explicitly to validate permission boundaries; data/BSS pages are currently zero-filled pages.

The ELF smoke uses a static freestanding ELF64 `ET_EXEC` image built by
`xmake build user-init-elf` and optionally packaged by `tools/boot_debug.py` at
`/boot/user/init.elf`. The ELF image writes `BIGOS_USER_ELF_WRITE\n` with
`SYS_WRITE`, then calls `SYS_EXIT(0)`.

## Process Lifecycle

`bigos::proc::Process` is a bounded single-core lifecycle record. The core is
compiled in normal builds even when `user_program_smoke` and `user_elf_smoke`
are disabled; those switches only control smoke entry threads and the user ELF
artifact. Each process records:

- Stable PID, parent PID, child/sibling links, process-table publication state, user page-table root, pre-entry kernel CR3, user entry, and user code/data/stack ranges.
- Dedicated syscall/exception kernel stack top for TSS/RSP0.
- Lifecycle state `Created` / `Running` / `Terminated` / `Faulted` / `Zombie` / `ReapPending` / `Reaped`, exit code, fault reason, owned user frames, and kernel stack range.
- Wait-status consumption and safe reaper metadata so PID reuse waits until a zombie has been consumed or is otherwise eligible for final reaping.

The process object and current kernel stack are not freed immediately from the
`exit` or fault return path. Termination records status, wakes eligible parent
waiters, and hands reclamation to a safe reaper after execution has switched to a
non-target kernel stack and non-active CR3 root.

## Loading And Address Space

The loader runs in non-interrupt context:

- `derive_user_address_space_root()` creates a user root with the low half cleared and the high half shared.
- `map_page_in_root()` maps code as `USER_CODE` and data/BSS/stack as `USER_DATA`.
- Load failure emits `BIGOS_USER_LOAD_FAILED` and halts, preventing entry into partially initialized ring3.
- Only `proc::run_user_process()` writes CR3 to activate the user root; ordinary derivation helpers do not switch address spaces implicitly.

The ELF loader accepts only bounded x86_64 little-endian ELF64 `ET_EXEC`
programs. It rejects unsupported program headers, W+X segments, overlapping
segments, entries outside executable segments, ranges outside the low-half user
window, and ranges colliding with the one-page stack at `USER_STACK_TOP`.
Successful ELF preparation emits `BIGOS_USER_ELF_LOAD_PASSED`; bounded load
failures emit `BIGOS_USER_ELF_LOAD_FAILED <reason>` and do not enter ring3. The
general exec primitive prepares a new image before commit, rolls back failures
before publication, and uses deterministic exec-failure status if the old image
cannot be resumed after commit.

The initial ELF user stack uses a minimal libc-like shape: `argc`, `argv[]`,
`envp[]`, and bounded strings. It intentionally omits auxv, TLS, dynamic linker
state, and user-space libc startup. Process-local file descriptors and the
read-only VFS shell are kernel-managed lifecycle state, not objects constructed
by the initial user stack.

## Ring3 Entry

x86_64 runtime user-mode support is provided by `src/kernel/proc/user_mode.cc` / `user_mode.s`:

- The new GDT keeps kernel code/data/stack selectors `0x08/0x10/0x18` unchanged.
- It adds user data selector `0x23`, user code selector `0x2b`, and TSS selector `0x30`.
- `init_user_mode()` loads the GDT and TSS; `set_tss_rsp0()` sets the kernel return stack before user entry.
- `enter_user_mode()` builds an `SS:RSP/RFLAGS/CS:RIP` frame and enters CPL3 with `iretq`.

## Syscall And Fault

- `VECTOR_SYSCALL = 0x80` is the only IDT gate relaxed to DPL=3; exception/IRQ gates are not relaxed.
- `SYS_WRITE` validates the user buffer range, present/user bits, and maximum length, then emits `BIGOS_USER_WRITE_SYSCALL`; invalid user buffers fault the process and use the same safe reaper boundary.
- `SYS_EXIT` marks the current process terminated/reap-pending, records the exit code, restores the kernel root, and enters the scheduler's deferred-reclamation exit path.
- `SYS_WAIT` exposes the minimal wait ABI and uses the same `sched::can_block()` guard as other blocking-capable syscall paths. Ordinary user process syscalls can block when the scheduler context and IF state allow it; unsupported contexts return deterministic wait errors.
- User-mode `#PF` is identified from the saved `CS` CPL, emits `BIGOS_USER_PAGE_FAULT`, marks the process faulted/reap-pending, and does not implement demand paging.
- Kernel-mode `#PF` keeps the existing diagnostic-only `BIGOS_PAGE_FAULT` semantics.
- The idle-loop reaper releases the user address space and kernel stack once the active stack/root checks pass, then emits `BIGOS_USER_RECLAIMED`.
