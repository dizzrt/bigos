# Bounded FD Multiplexing (SYS_POLL)

BigOS adds a bounded `poll(2)`-style multiplexing syscall on top of the unified
fd readiness model and the scheduler wait queues, so a single-threaded user
program can wait on a fixed-capacity descriptor set with a millisecond timeout
and write an event loop. It reuses the existing readiness snapshot
(`vfs::poll_file`) and blocking primitives; it does not imply complete POSIX
`poll`/`select`/`epoll`/`ppoll`, unbounded descriptor sets, edge-triggered or
event-object semantics, or signal-interrupted restart.

## Append-Only ABI: SYS_POLL = 61

- `include/bigos/syscall.h` appends `SYS_POLL = 61` (the previous maximum was
  `SYS_DYN_PROTECT = 60`). No existing syscall number, register argument order,
  `int 0x80` return convention, syscall gate DPL, or exception/IRQ no-EOI rule
  changes.
- Register convention follows the fixed ABI: `rdi = user pollfd*`, `rsi = nfds`,
  `rdx = timeout_ms`; the return value in `rax` is the ready descriptor count
  (0 on timeout with none ready) or a negative errno.
- User-visible structure (kernel `bigos::sys::pollfd` and the `user/libc`
  mirror stay identical):

  ```
  struct pollfd {
      int32_t  fd;       // watched descriptor; negative to ignore this entry
      uint16_t events;   // requested event bits (POLLIN/POLLOUT subset)
      uint16_t revents;  // kernel-filled ready bits (may add POLLERR/POLLHUP/POLLNVAL)
  };
  ```

- Event bits (values aligned with common Linux `poll(2)` for portability):
  `POLLIN = 0x001`, `POLLOUT = 0x004`, `POLLERR = 0x008`, `POLLHUP = 0x010`,
  `POLLNVAL = 0x020`. `POLLERR`/`POLLHUP`/`POLLNVAL` are output-only: the kernel
  fills them under the corresponding condition even if `events` did not request
  them.
- `nfds` is bounded by `POLL_MAX_FDS` (16). `nfds > POLL_MAX_FDS` returns
  `-EINVAL`; `nfds == 0` is legal (a degenerate timeout-only wait).
- `timeout_ms` semantics: `> 0` blocks with a millisecond deadline; `== 0` does a
  single readiness scan and returns immediately (non-blocking probe); `< 0` waits
  without a deadline. Millisecond-to-tick conversion reuses the coarse monotonic
  tick used by `SYS_SLEEP_MS`.

## Level-Triggered Readiness Reusing poll_file

For each `fd >= 0`, `proc::poll_fds_current` (`kernel/core/proc/proc.cc`)
resolves the descriptor with `file_for_fd_current` and queries
`vfs::poll_file(File*)`, mapping the kernel-internal `READY_*` bits to
user-visible `revents`:

- `READY_READABLE` -> `POLLIN` (only when `events & POLLIN`)
- `READY_WRITABLE` -> `POLLOUT` (only when `events & POLLOUT`)
- `READY_ERROR` -> `POLLERR | POLLHUP` unconditionally (peer close / error)

The mapping is level-triggered: as long as a condition holds it is reported. The
readiness query is the same one non-blocking reads/writes and blocking predicates
use, so "poll reports readable" holds exactly when a non-blocking read would not
return would-block. The scan is read-only: it dequeues nothing and changes no
descriptor offset or open state.

## Bad And Negative Descriptors

- An invalid/unopened `fd` fills `POLLNVAL` in that entry and counts it as ready,
  without failing the whole call (matching `poll(2)`).
- A negative `fd` entry is ignored: its `revents` is cleared and it neither counts
  as ready nor participates in blocking.

## Scheduler Multi-Queue Registration Blocking

When nothing is ready, `timeout_ms != 0`, and the context can block,
`poll_fds_current` collects each valid descriptor's wait queues and blocks:

- `vfs::file_poll_wait_queues` dispatches to an appended read-only
  `FileOperations::poll_wait` op. pipe returns `read_wq` (read end) or `write_wq`
  (write end); socket returns the endpoint `rx_wait`; tty returns the shared
  global input wait queue. A backend without the op (e.g. a regular file)
  contributes no queue, because `poll_file` reports it always ready.
- The collected queues are de-duplicated (bounded linear scan, up to
  `sched::POLL_MAX_WAIT_QUEUES = 32`) and passed to a new scheduler primitive
  `sched::wait_queue_wait_any(queues, count, predicate, arg, timeout_ticks)`.
- `wait_queue_wait_any` (`kernel/core/sched/sched.cc`) is an append-only
  extension of the single-queue wait path:
  - `WaitQueue` gains an appended `poll_head` list head (existing `head`/`tail`/
    `lock` unchanged, `static_assert`-guarded); each TCB gains a fixed
    `PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES]` array in stable per-thread
    storage, so registration and wakeup never allocate.
  - It registers one poll node per queue with IRQs disabled inside the scheduler
    critical section, then checks the predicate. If already satisfied it
    unregisters and returns without blocking. Otherwise it records a deadline (for
    a positive timeout) on the existing sleep list, blocks, and yields.
  - On resume (any queue's wake or timeout) the woken thread self-removes its
    poll nodes from every queue it registered on, under each queue's lock.
  - `wake_one`/`wake_all` additionally drain the target queue's `poll_head`:
    each poll node's owner is made runnable idempotently (reusing
    `wake_thread_locked`) and the node is removed from that queue only. Producers
    hold only their own queue lock and take each owner's domain lock separately
    (never both at once); cross-queue cleanup is left to the woken thread. Because
    readiness is level-triggered, a spurious or duplicate wake only costs one
    extra re-scan.
- After the wait returns, the descriptor set is re-scanned to refill `revents`
  and recount. The scheduler private `WAIT_OK`/`WAIT_TIMEOUT` result is not
  surfaced: a timeout with nothing ready simply yields the current ready count
  (typically 0).

Single-queue `wait_queue_wait_until`/`wake_one`/`wake_all` semantics are
unchanged; a queue that never had a multi-queue waiter keeps an empty
`poll_head`, so `drain_poll_head` is a no-op for it.

## Immediate-Return Fast Path

`poll_fds_current` returns immediately (without registering on any wait queue)
when the first scan finds something ready, when `timeout_ms == 0`, or when the
context cannot block (`!sched::can_block()`), consistent with the non-blocking
short-circuit used elsewhere.

## Syscall Wrapper And User libc Mirror

- `sys_poll` (`kernel/core/syscall/syscall.cc`) validates `nfds <= POLL_MAX_FDS`,
  validates and copies the user array into a fixed kernel work buffer with
  `validate_user_io_buffer` / `copy_current_user_buffer`, runs the shared
  `proc::poll_fds_current` core, then copies the array back with
  `copy_to_current_user_buffer`. `nfds == 0` skips the user-buffer access.
- `user/libc/include/poll.h` defines the matching `struct pollfd`, the `POLL*`
  event bits, `POLL_MAX_FDS`, and `int poll(struct pollfd *, unsigned long nfds,
  int timeout);`, with a header note describing the bounded subset. The
  `user/libc/syscall.c` wrapper forwards `SYS_POLL` (`rdi/rsi/rdx`) and translates
  a negative return into `errno` with a `-1` result, staying freestanding-safe.

## Validation

- A default-off xmake switch `--fd_multiplexing_smoke=y` maps to the
  `BIGOS_FD_MULTIPLEXING_SMOKE` macro, following the existing smoke option
  pattern. It is off by default and does not change default boot behavior.
- The smoke entry (`bigos::proc::fd_multiplexing_smoke_entry`, spawned from
  `kernel/core/kernel.cc`) runs from a blockable kernel thread inside a bounded
  process context (registered with the scheduler through
  `set_current_user_process` so a blocking poll that yields to init/shell has its
  process slot restored on resume). It asserts: `nfds > POLL_MAX_FDS` returns
  `-EINVAL`; a zero timeout is a non-blocking probe returning immediately; a
  positive timeout with nothing ready truly blocks (a monotonic-tick delta
  witnesses the yield) and returns 0; a bad fd entry is marked `POLLNVAL` and
  counted while a negative fd entry is ignored, without failing the call; a
  producer thread writing to a pipe while poll blocks wakes the waiter and reports
  only that entry; and "poll readable" implies a non-blocking read does not
  would-block. It emits a deterministic `BIGOS_FD_MULTIPLEXING_PASSED` /
  `BIGOS_FD_MULTIPLEXING_FAILED` COM1 marker, validated through the QEMU headless
  path.
- The kernel/user contract (`SYS_POLL = 61` appended without renumbering, the
  `pollfd` layout, the `POLL*` event bits, and `POLL_MAX_FDS`) is enforced by the
  source contract test `tests/test_syscall_entry_source.py`.

## Non-Goals

- No complete POSIX `poll`/`select`/`epoll`/`ppoll`; no unbounded or dynamically
  growing descriptor sets (the set is fixed-capacity, over-limit is a
  deterministic `-EINVAL`).
- No edge-triggered readiness, event notification objects (eventfd/epoll fd),
  `POLLPRI`/out-of-band data, or `POLLRDHUP`.
- No `-EINTR` signal-interrupt restart semantics beyond the underlying bounded
  blocking primitive's behavior.
- Multiplexing adds no new readiness semantics for regular files or block
  devices: those stay always-ready under the readiness model, so poll returns
  them immediately.
- No change to existing syscall numbers, the `int 0x80` ABI, boot/linker/vector/
  page-table/disk layout, or the single-queue scheduler wait/wake semantics.
