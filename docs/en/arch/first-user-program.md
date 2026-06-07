# First User Program Runtime Path

Stage 6 `user_program_smoke` is a default-off validation path that proves BigOS can create a minimal user process, load an embedded user program, enter CPL3, and return to the kernel through `write` / `exit` syscalls.

## Image Format

The first user program uses an embedded flat blob rather than ELF64 or filesystem loading:

- The flat blob is provided by the `FIRST_USER_CODE` byte sequence in `src/kernel/proc/proc.cc` and linked into the kernel image.
- The image does not depend on an in-kernel FS, block devices, hosted OS file IO, or bootloader-only exFAT helpers.
- The blob executes only a bounded `SYS_WRITE(fd=1, buf, len)` and then `SYS_EXIT(0)`.
- The loader still maps code, data/BSS, and stack explicitly to validate permission boundaries; data/BSS pages are currently zero-filled pages.

## Process Model

`bigos::proc::Process` is a bounded single-core, single-process abstraction. It records:

- `pid`, user page-table root, pre-entry kernel CR3, user entry, and user code/data/stack ranges.
- Dedicated syscall/exception kernel stack top for TSS/RSP0.
- Lifecycle state `Created` / `Running` / `Terminated` / `Faulted` / `Reaped`, exit code, fault reason, owned user frames, and kernel stack range.

The process object and current kernel stack are not freed immediately from the `exit` or fault return path. Reclamation is handed to a safe reaper after execution has switched to a non-target kernel stack.

## Loading And Address Space

The loader runs in non-interrupt context:

- `derive_user_address_space_root()` creates a user root with the low half cleared and the high half shared.
- `map_page_in_root()` maps code as `USER_CODE` and data/BSS/stack as `USER_DATA`.
- Load failure emits `BIGOS_USER_LOAD_FAILED` and halts, preventing entry into partially initialized ring3.
- Only `proc::run_user_process()` writes CR3 to activate the user root; ordinary derivation helpers do not switch address spaces implicitly.

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
- User-mode `#PF` is identified from the saved `CS` CPL, emits `BIGOS_USER_PAGE_FAULT`, marks the process faulted/reap-pending, and does not implement demand paging.
- Kernel-mode `#PF` keeps the existing diagnostic-only `BIGOS_PAGE_FAULT` semantics.
- The idle-loop reaper releases the user address space and kernel stack once the active stack/root checks pass, then emits `BIGOS_USER_RECLAIMED`.
