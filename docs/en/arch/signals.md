# Minimal Signals

This stage adds a minimal, correct POSIX signal model on top of the existing
process model (fork/COW), the identity/permission primitives, the `int 0x80`
syscall entry, and the guarded IRQ-return reschedule hook. It is intentionally
small: fixed non-realtime signal numbers, per-process inline signal state, a
single IRQ-return delivery point, and bounded process-group delivery for default
terminal interrupt-like input. It does not introduce realtime signals, complete
job control, `termios`, cross-core signal broadcast semantics, or a complete
POSIX libc.

## Signal Numbers And Default Actions

`include/bigos/signal.h` defines a fixed set of non-realtime signal numbers that
reuse the conventional POSIX/Linux values: `SIGINT = 2`, `SIGKILL = 9`,
`SIGUSR1 = 10`, `SIGSEGV = 11`, `SIGUSR2 = 12`, `SIGTERM = 15`, `SIGCHLD = 17`.
Valid numbers are `1..SIG_MAX` with `SIG_MAX = 31`, so every signal maps to a single bit
`1ull << (signo - 1)` inside a per-process `uint64_t` bitmap (`SigSet`). The
bitmap width and the highest signal number agree and stay `<= 64`.

Each signal has a deterministic default action:

- Terminate: the default for `SIGINT`, `SIGKILL`, `SIGTERM`, `SIGSEGV`, `SIGUSR1`,
  `SIGUSR2`, and any other number with no special default.
- Ignore: the default for `SIGCHLD`.

Repeated delivery of the same signal before it is taken merges into one pending
bit (standard non-realtime semantics; no realtime queuing or counting).
`SIGKILL` is uncatchable and unblockable: it always terminates regardless of the
recorded disposition or blocked mask.

## Per-Process Signal State

`Process` gains three appended, fixed-size inline fields (the earlier layout is
unchanged):

- `sig_pending` (`SigSet`): the pending-signal bitmap.
- `sig_mask` (`SigSet`): the blocked mask. A blocked signal stays pending until
  unblocked; `SIGKILL` can never be added to the mask.
- `sig_disp[SIG_COUNT]` (`SigDisposition`): the per-signal disposition table
  indexed by `signo - 1`, each entry being `Default`, `Ignore`, or `Handler`
  with a user-mode handler entry address.

All signal delivery and query paths operate on these inline fields and never
allocate or block.

Lifecycle:

- init (PID 1) and non-fork ELF creation: `signal::init_state` sets all
  dispositions to default, an empty mask, and empty pending.
- `fork_current`: `signal::inherit_on_fork` copies the disposition table and
  blocked mask field-by-field into the child and clears the child's pending set
  (POSIX fork). It adds no allocation and no new failure path and does not change
  the COW / reference-count / rollback or parent-returns-child-PID / child-
  returns-0 semantics.
- `exec`: `signal::reset_handlers_on_exec` resets every `Handler` disposition to
  default (the old handler entry is now an invalid user address) while preserving
  the blocked mask and pending set (decision 10).

## kill Delivery And Permission Enforcement

`signal::kill(target, signo)` sets the target's pending bit for a valid signal,
returning `-EINVAL` for an out-of-range number. It makes no permission decision.
The `SYS_KILL` syscall is the single enforcement point: it looks up the target
PID (absent target -> `-ESRCH`), enforces `cred::may_signal(actor, target)`
(denied -> `-EPERM`, target pending untouched), and only then calls
`signal::kill` (invalid signal -> `-EINVAL`, success -> 0). The `cred::may_signal`
decision logic is unchanged (root allowed, otherwise identity match, null
inputs rejected).

The default terminal may also target the current numeric foreground process
group with bounded `SIGINT` when interrupt-like input is consumed outside IRQ
context. The keyboard IRQ only enqueues input and wakes a waiter; process-group
traversal, permission checks, and pending-bit updates happen in ordinary
user-process syscall context.

## Delivery At The IRQ-Return-To-User Boundary

Signals are delivered at exactly one point (decisions 2 and 9): in the external
IRQ branch of `irq_dispatch`, after `sched::maybe_preempt_on_irq_return` and
before `iretq`, and only when the interrupted frame is user-mode
(`(cs & 0x3) == 0x3`), a current process exists, and it has a deliverable signal
(unblocked, or unblockable like `SIGKILL`). Kernel-mode interrupted frames never
deliver user signals, and this path sends no i8259 EOI of its own.

`signal::deliver_pending_to_user` selects the lowest-numbered deliverable signal,
clears its pending bit, and acts on the disposition:

- `SIGKILL` or a default Terminate disposition routes through the existing
  `fault_current_and_exit` lifecycle, encoding the signal as
  `-(128 + signo)` into the exit/fault status so the parent's `wait` or
  diagnostics can observe which signal killed the process. The existing
  zombie/reaper, `wait_status_consumed`, and `parent_waiting` semantics are
  unchanged.
- An Ignore disposition (e.g. `SIGCHLD` with no handler) clears the bit and
  returns to user space unchanged.
- A user `Handler` disposition builds an on-user-stack signal frame and redirects
  the interrupted context (see below).

Because this is the only delivery point, a self-targeted signal (e.g.
`kill(getpid(), ...)`) is delivered at the next return-to-user boundary rather
than synchronously on the `SYS_KILL` return. Under the bounded timer interrupt
model, a running user process passes that boundary within an observable tick, so
the delay is bounded and deterministic.

## Signal Frame And sigreturn

For a user-handler disposition, `deliver_pending_to_user`:

1. Computes a frame address below the interrupted `rsp`, leaving the System V
   red zone (128 bytes) untouched and 16-byte aligning the result.
2. Validates the frame range with the VMA-backed writable-user-buffer check
   (`validate_user_io_buffer`) and the user low-half limit. Any failure
   deterministically terminates the process (equivalent to default `SIGSEGV`)
   and never writes an illegal user address, allocates, or blocks.
3. Saves the interrupted user-visible registers plus `rip`/`rsp`/`rflags`, the
   signal number, the pre-handler blocked mask, and a `SIGFRAME_MAGIC` word into
   a `SignalFrame` on the user stack.
4. Adds the delivered signal to the blocked mask for the duration of the handler,
   then rewrites the interrupted frame: `rip = handler`, `rdi = signo` (System V
   first argument), `rsp = frame address`.

The handler ends by invoking `SYS_SIGRETURN`. `signal::sigreturn` reads the
`SignalFrame` from `rsp`, rejects a missing/forged magic or a `rip`/`rsp` outside
the user low half (deterministic terminate), restores the user-visible registers
and `rip`/`rsp`, forces the user code/data segment selectors and a sanitized
`rflags` (IF on, reserved bit set, privileged bits cleared), and restores the
pre-handler blocked mask (never re-adding `SIGKILL`). It never returns to a
kernel privileged context from user-controlled data. There is no libc trampoline
this stage; the smoke user program (and any handler) must invoke `SYS_SIGRETURN`
explicitly.

## New Syscalls

The `int 0x80` ABI appends four numbers after `SYS_GETGID = 15` without changing
any existing number or the register convention (number -> rax, args ->
rdi/rsi/rdx/r10/r8/r9, return -> rax). None send an i8259 EOI or relax any
exception/IRQ gate or DPL.

- `SYS_KILL = 16` (pid, signo) -> 0 or `-ESRCH`/`-EPERM`/`-EINVAL`.
- `SYS_SIGACTION = 17` (signo, action, handler, old_out) -> 0 or `-EINVAL`.
  Setting a handler or ignore for `SIGKILL` returns `-EINVAL`.
- `SYS_SIGPROCMASK = 18` (how, set, old_out) -> 0 or `-EINVAL`. Unblockable bits
  in the requested set are silently dropped.
- `SYS_SIGRETURN = 19` restores the interrupted user context from the user-stack
  signal frame. It is the only syscall that rewrites the saved user context
  (including `rax`) directly into the frame, so it returns before the shared
  `rax` write-back; it does not break the `InterruptFrame` layout contract.

`ESRCH` and `EPERM` are added to `include/bigos/errno.h` (single source of truth;
not redefined in subsystem sources).

## Validation

The default-off `signal_smoke` (`xmake f --signal_smoke=y`, `BIGOS_SIGNAL_SMOKE`)
emits `BIGOS_SIGNAL_PASSED` / `BIGOS_SIGNAL_FAILED`. It covers the fixed
numbers / default actions / `SIGKILL` uncatchable+unblockable invariants, kill
setting a pending bit, the blocked mask deferring then releasing delivery, a
user-handler delivery building the on-user-stack frame and a `sigreturn`
restoring it exactly, and an over-privileged kill being rejected by
`cred::may_signal`. The existing smoke matrix and default boot markers are
unchanged.

Like the other process smokes, this default-off smoke creates and tears down a
user process before init runs, so the smoke build observes a
`BIGOS_INIT_LOAD_FAILED map-failed` afterward; this is a smoke-mode artifact, not
normal-boot behavior. Source-contract / behavior-assertion tests under `tests/`
fix the new syscall numbers, the `Process` signal fields, the no-allocation
delivery path, the `SIGKILL` invariants, the `may_signal` wiring on kill, and the
user-frame-only IRQ-return delivery point.

## Non-Goals And Known Limitations

Out of scope this stage: realtime signals (`SIGRTMIN`+ queuing) and full
`siginfo`/`sigqueue`; `SIGSTOP`/`SIGCONT` job control, process groups, and
`killpg`; `sigsuspend`/`sigpending`/`sigaltstack` and full `sigaction` flags;
`SA_RESTART` / EINTR syscall restart (the synchronous syscalls do not sleep);
core dump files; a user-space libc signal wrapper and automatic trampoline; and
broad cross-core signal broadcast/delivery policy beyond the current bounded
process model.

Known limitation: with a single IRQ-return delivery point, a self-targeted signal
is delivered at the next return-to-user boundary, not synchronously on the
`SYS_KILL` return.
