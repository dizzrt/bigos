# Kernel FD Readiness Model

BigOS unifies the previously scattered descriptor readiness checks (pipe
`read_ready`/`write_ready`, tty `input_available`) into a single kernel-internal
fd readiness query and adds the receive wait queue UDP sockets were missing. It
is a kernel-internal foundation for later non-blocking descriptors and
multiplexing syscalls; it does not add a user-visible multiplexing syscall and
does not imply complete POSIX `poll`/`select`/`epoll` semantics.

## Unified Query Entry

- `include/bigos/fs/vfs.h` defines kernel-internal readiness bit flags
  `READY_READABLE`, `READY_WRITABLE`, and `READY_ERROR`. They combine with
  bitwise OR and are a kernel-only convention, not a user-visible syscall ABI.
- `FileOperations` gains one appended optional op `PollOp poll` after the
  existing `read/close/write/lseek/truncate/readdir` slots. Existing slots are
  not reordered; `static_assert` offset guards keep the appended slot last.
- `vfs::poll_file(File*)` is the single entry. When `ops->poll` is present it is
  called; otherwise the entry returns the deterministic default
  `READY_READABLE | READY_WRITABLE` (a regular file is always readable and
  writable). A null file returns `READY_ERROR`.
- `poll_file` is a pure read-only snapshot: it never dequeues data, blocks the
  caller, or changes the file or backend open state. Each backend's `poll`
  reuses the same predicate its blocking read/write path waits on, so "poll
  reports readable" holds exactly when a blocking read would not block.

## Backend Semantics

- pipe (`kernel/core/ipc/pipe.cc`): the read end is readable when the buffer has
  data or the write end is closed (readable EOF); the write end is writable when
  the buffer has space or the read end is closed, and reports `READY_ERROR` when
  the read end is closed (broken-pipe tendency). It reuses `read_ready` and
  `write_ready` directly.
- socket (`kernel/core/net/socket.cc`): a bound, active UDP endpoint with a
  non-empty receive queue is readable, an endpoint able to send is writable, and
  an unbound or inactive endpoint reports `READY_ERROR`.
- tty (`kernel/core/terminal/tty.cc`): `TTY_OPS.poll` reuses `input_available`
  (input ring records or a pending escape-sequence byte) for the readable bit;
  the terminal write-out direction is always writable and the input path
  produces no error bit. The op dequeues no `TerminalInputRecord` and does not
  touch the input ring head/tail. Because fd 0/1/2 and any `dup` copy share one
  `TTY_OPS` handle, all terminal descriptors report identical readiness with no
  raw-fd special case.
- Other backends (exFAT, bigfs, file-mapping smoke) leave `poll` null and get
  the deterministic readable+writable default.

## Socket Receive Wait Queue

- `UdpEndpoint` (`include/bigos/net.h`) gains a `sched::WaitQueue rx_wait`,
  initialized at `udp_bind`.
- The protocol RX delivery path (`handle_udp` in `kernel/core/net/protocol.cc`)
  enqueues the datagram into `rx_queue` first and then calls
  `sched::wake_all(&rx_wait)`. The wakeup is allocation-free and safe to call
  from the IRQ/delivery context, following the existing wake convention
  (enqueue before wake closes the missed-wakeup window).
- `sys_recvfrom` (`kernel/core/syscall/syscall.cc`) keeps its existing external
  behavior unchanged: with no data and an unblockable context it still returns
  `-EWOULDBLOCK` (`NoData` maps to `-EAGAIN`). This change only adds the wait
  queue and readiness query; it does not change the return codes or the bounded
  poll-and-yield contract.

## Validation

- A default-off xmake switch `--fd_readiness_smoke=y` maps to the
  `BIGOS_FD_READINESS_SMOKE` macro, following the existing smoke option pattern.
  It is off by default and does not change default boot behavior.
- The smoke entry in `kernel/core/kernel.cc` builds pipe, UDP socket, and tty
  descriptors and asserts: poll's readable bit agrees with the blocking behavior
  ("readable then a blocking read returns immediately; not readable then no
  data"), poll is a non-dequeuing snapshot, pipe readable-EOF and broken-pipe
  error bits, socket empty/non-empty/drained readiness with the RX wakeup, and
  that two terminal fds sharing `TTY_OPS` report identical readiness. It emits a
  deterministic `BIGOS_FD_READINESS_PASSED` / `BIGOS_FD_READINESS_FAILED` COM1
  marker, validated through the QEMU headless path.

## Non-Goals

- No non-blocking read/write descriptor flag (`O_NONBLOCK`/`F_GETFL`/`F_SETFL`)
  is added here.
- No user-visible multiplexing syscall (`poll`/`select` class) is added; this
  change introduces no new syscall number and does not change the syscall ABI.
- The readiness bits are a kernel-internal convention only; a user-visible
  POSIX-style event encoding is defined separately later and converted at a
  single point. No complete POSIX `poll`/`select`/`epoll` semantics and no
  "real readiness" for regular files, block devices, or other descriptor types
  are claimed.
