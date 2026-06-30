# Bounded Nonblocking FD I/O

BigOS adds a bounded `O_NONBLOCK` subset on top of the unified fd readiness
model so a single-threaded user program can let reads, writes, and receives
return a deterministic would-block status instead of blocking. It reuses the
existing readiness predicates and blocking primitives; it does not imply complete
POSIX `O_NONBLOCK`, async I/O, or a user-visible multiplexing syscall, and it
adds no new syscall number.

## Nonblocking Flag On The Open File Description

- `include/bigos/fs/vfs.h` appends one boolean `nonblocking` to `vfs::File`
  (default false). The earlier layout is not reordered; `static_assert` offset
  guards keep `nonblocking` appended after `identity` (which stays after
  `writable`).
- The flag lives on the open file description, so `dup`/`dup2`/`fork` share it
  naturally because those paths share the same `vfs::File` through
  `vfs::retain`. Setting the flag through any descriptor referencing that open
  file description is visible through all of them.
- Two kernel-internal helpers back the would-block decision points and
  fd-control: `file_is_nonblocking(File*)` reads the flag (a null file reports
  false, so callers fall back to the `!can_block()` short-circuit), and
  `file_access_mode(File*)` synthesizes the `OPEN_RDONLY`/`OPEN_WRONLY`/
  `OPEN_RDWR` access bits from `readable`/`writable` for `F_GETFL`.
- The single kernel source of `O_NONBLOCK` is `vfs::OPEN_NONBLOCK` (`1 << 11`),
  mirrored by `bigos::proc::O_NONBLOCK` and the user libc `O_NONBLOCK`. The value
  does not collide with the `OPEN_*` access/creation flags.

## fd-control: F_GETFL / F_SETFL

- `include/bigos/proc.h` adds `FCNTL_F_GETFL = 3` and `FCNTL_F_SETFL = 4`
  alongside the unchanged `FCNTL_F_DUPFD = 0` / `FCNTL_F_GETFD = 1` /
  `FCNTL_F_SETFD = 2`.
- `proc::fcntl_fd_current` (`kernel/core/proc/proc.cc`):
  - `F_GETFL` returns the synthesized access mode bitwise-ORed with `O_NONBLOCK`
    when the open file description's nonblocking flag is set. It leaves the fd,
    offset, reference, and close-on-exec state unchanged.
  - `F_SETFL` is a single `nonblocking = (arg & O_NONBLOCK) != 0`. Every other
    bit (access-mode bits, creation bits, any unimplemented status bit) is
    ignored without error and the call returns success. This is the only policy
    that does not break the standard idiom `F_SETFL(F_GETFL | O_NONBLOCK)`, whose
    argument necessarily carries the access-mode bits. It does not touch the
    access mode or `FdEntry.close_on_exec`.
- `sys_fcntl` (`kernel/core/syscall/syscall.cc`) keeps the `can_block()` guard
  only on `F_DUPFD` (the only command that may grow the fd table / allocate).
  `F_GETFL`/`F_SETFL` are pure flag reads/writes and need no guard.

## Backend Would-Block Short-Circuits

Each backend's existing "about to block" decision point is the nonblocking
short-circuit point. The rule is uniform: only when an operation would have to
wait, a nonblocking descriptor (or a non-blockable context) returns would-block,
reusing the same predicate the blocking path waits on (so the readiness query
and the would-block behavior cannot drift).

- pipe (`kernel/core/ipc/pipe.cc`): `pipe_read` on an empty pipe with the writer
  open and `pipe_write` on a full pipe with the reader open extend the existing
  `if (!can_block())` condition to
  `if (file_is_nonblocking(file) || !can_block())`, returning `WouldBlock`. A
  write that already wrote some bytes returns those bytes (existing `done > 0`
  behavior) rather than would-block.
- tty (`kernel/core/terminal/tty.cc`): the non-blockable context keeps its strict
  short-circuit. Additionally, a nonblocking terminal read with no input
  available returns `WouldBlock` using the same `input_available` predicate
  `tty_poll` uses; it dequeues no input record and does not touch the input ring.
  The terminal write-out direction is always writable, so nonblocking does not
  change it. Because fd 0/1/2 and any `dup` copy share one terminal open file
  description, the nonblocking flag is observed in lockstep across them (POSIX
  shared-OFD semantics); a program needing independent blocking behavior must
  reopen the terminal to get a new open file description.
- socket (`sys_recvfrom` in `kernel/core/syscall/syscall.cc`): a nonblocking
  socket fd does a single bounded RX advance (`recv_rounds = 1`) and skips
  `sched::yield()`; with no datagram it returns `-EAGAIN`. A blocking socket fd
  keeps the existing bounded poll-and-yield rounds and return codes unchanged.

`WouldBlock` maps through the existing `vfs::Status` -> errno path to
`-EWOULDBLOCK` (equal to `-EAGAIN`, value 11), so the user-visible return code is
deterministic.

## Readiness Consistency Contract

For one descriptor at one instant, `poll_file` reporting `READY_READABLE` holds
exactly when a nonblocking read would not return would-block, and
`READY_WRITABLE` exactly when a nonblocking write would not return would-block.
Each backend enforces this by reusing the same readiness/blocking predicate at
its would-block decision point rather than maintaining a second condition that
could drift. This is the prerequisite for a later multiplexing syscall to drive
nonblocking descriptors correctly.

## User libc Mirror

- `user/libc/include/fcntl.h` adds `O_NONBLOCK` (`1 << 11`, same value as the
  kernel), `F_GETFL = 3`, and `F_SETFL = 4`, with a header note describing the
  bounded `O_NONBLOCK` subset (not full POSIX status-flag handling).
- The `fcntl` wrapper in `user/libc/syscall.c` forwards the variadic argument for
  `F_SETFL` (in addition to `F_DUPFD`/`F_SETFD`); `F_GETFL` takes no argument and
  passes 0. The wrapper stays freestanding-safe and keeps the existing errno
  translation.

## Validation

- A default-off xmake switch `--nonblocking_fd_smoke=y` maps to the
  `BIGOS_NONBLOCKING_FD_SMOKE` macro, following the existing smoke option
  pattern. It is off by default and does not change default boot behavior.
- The smoke entry (`bigos::proc::nonblocking_fd_smoke_entry`, spawned from
  `kernel/core/kernel.cc`) runs from a blockable kernel thread over a real fd
  table and asserts: `F_GETFL`/`F_SETFL` round-trip including the access mode;
  `F_SETFL(F_GETFL | O_NONBLOCK)` carrying access-mode bits still succeeds and
  only changes the nonblocking bit; a nonblocking empty-pipe read and a
  nonblocking full-pipe write return would-block while clearing the flag restores
  the blocking-capable path; `poll_file` readable matches a non-would-block read;
  a nonblocking tty read with no input returns would-block; terminal fd 0/1/2
  observe the nonblocking bit in lockstep through `F_GETFL`; and a nonblocking
  bound socket with an empty receive queue reports not-readable. It emits a
  deterministic `BIGOS_NONBLOCKING_FD_PASSED` / `BIGOS_NONBLOCKING_FD_FAILED`
  COM1 marker, validated through the QEMU headless path.
- The kernel/user constant agreement (`O_NONBLOCK`/`F_GETFL`/`F_SETFL` values and
  non-collision with the open flags) is enforced by the source contract test
  `tests/test_syscall_entry_source.py`.

## Non-Goals

- No complete POSIX `O_NONBLOCK`: regular files, block devices, and directories
  keep their synchronous-completion semantics and produce no would-block return
  beyond this capability.
- No `O_ASYNC`/signal-driven I/O, record locking, `F_DUPFD_CLOEXEC`, or
  descriptor passing.
- No user-visible multiplexing syscall (`poll`/`select` class); this change adds
  no new syscall number and does not change the `int 0x80` ABI, boot/linker/
  vector/page-table/disk layout.
- `O_NONBLOCK` is not wired into `open()` initial-flag parsing; it is set only
  through `F_SETFL`.
