# System Call Entry

BigOS uses a controlled software-triggered kernel entry path and a minimal syscall ABI. The ring0 diagnostic syscall remains available. The default-off `user_program_smoke` path configures GDT/TSS and a user address space, then allows CPL3 to enter the same dispatcher through `int 0x80`.

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
- `SYS_WAIT` (number=4): legacy raw wait shape for `WAIT_ANY` or a specific child pid with an optional user `int*` status output. It preserves the existing two-argument ABI.
- `SYS_OPEN` (number=5): copies a bounded NUL-terminated user path, accepts the bounded open flags implemented by VFS (`O_RDONLY`/`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`), opens through the VFS shell, and returns a process-local fd.
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

Process group/session and default-terminal foreground controls are appended at
numbers 41..46: `SYS_GETPGID`, `SYS_GETSID`, `SYS_SETPGID`, `SYS_SETSID`,
`SYS_TCGETPGRP`, and `SYS_TCSETPGRP`. They operate only on the bounded single
default terminal model, return deterministic POSIX-style negative errno, and do
not change the `int 0x80` register ABI, syscall vector, IDT DPL, or EOI rules.
They are not complete POSIX job control, `tcsetpgrp(3)` semantics, `termios`,
multiple terminals, background jobs, or a complete POSIX process model.

Default-terminal mode controls are appended at numbers 51..52:
`SYS_TCGETMODE` and `SYS_TCSETMODE`. They use `rdi` as a user pointer to the
fixed BigOS terminal-mode object and expose only canonical/raw input mode for
the single default console terminal. `SYS_TCGETMODE` copies out a deterministic
snapshot and does not mutate terminal, fd, process, or foreground state.
`SYS_TCSETMODE` validates object size/version/flags/mode and requires the
caller to be in the current foreground process group, except that a session
leader recovery path may restore canonical mode only. The syscalls are
append-only and do not imply POSIX `tcgetattr`/`tcsetattr`, complete `termios`,
baud rates, `VMIN/VTIME`, pseudo-terminals, background read/write control, or
complete job control.

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
- `SYS_MAP_FILE` (number=35): takes `rdi` = fd, `rsi` = page-aligned file offset,
  `rdx` = page-aligned length, `r10` = permissions, and `r8` = reserved flags
  (must be 0). It publishes a bounded read-only, private, lazily backed
  file-backed mapping in the user file-mapping window and returns the mapped base
  user address, or a deterministic negative errno
  (`-EBADF`/`-EACCES`/`-EINVAL`/`-ENOMEM`/`-EWOULDBLOCK`). The fd must reference a
  readable regular file, permissions must be read-only and non-W+X, and the
  request publishes no partial VMA on failure. Covered pages materialize on first
  read access through the page/buffer cache; writes to the read-only pages,
  out-of-range access, and cache load from a non-blocking context are
  deterministic user faults or syscall failures.
- `SYS_UNMAP_ANON` (number=36): takes `rdi` = page-aligned user address and
  `rsi` = page-aligned non-zero length. It only accepts ranges fully covered by
  compatible private anonymous VMAs in the user low half. On success it removes
  or splits affected VMAs, clears present user PTEs, releases owned/COW-shared
  frames through the frame-reference helper, reclaims empty dynamic user
  page-table pages when ownership metadata permits, and invalidates affected
  current-CPU translations.
- `SYS_PROTECT_ANON` (number=37): takes `rdi` = page-aligned user address,
  `rsi` = page-aligned non-zero length, and `rdx` = BigOS permission bits
  (`Read=1`, `Write=2`, `Execute=4`). It only accepts compatible private
  anonymous VMAs, rejects W+X and unsupported backing, stages required VMA
  splits before publishing metadata, and updates present PTE permissions so page
  tables are no wider than VMA policy.
- `SYS_RMDIR` (number=38): takes `rdi` = user path and removes an empty directory
  on the writable `/rw` backend. It rejects regular files, non-empty directories,
  missing paths, read-only backend targets, invalid user paths, and nonblocking
  context with deterministic negative errno values.
- `SYS_FTRUNCATE` (number=39): takes `rdi` = fd and `rsi` = bounded length. It
  only accepts writable `/rw` regular files, updates size metadata on success,
  makes extended ranges read as zero, releases truncated tail blocks for safe
  reuse, and rejects directories, read-only backends, oversized lengths, bad fds,
  and nonblocking context with deterministic negative errno values.
- `SYS_WAITPID` (number=47): append-only bounded wait variant. ABI: `rdi` =
  `WAIT_ANY` or a positive child pid, `rsi` = optional user `int*` status output,
  and `rdx` = options. Only `options == 0` and `WNOHANG` are supported; process
  group selectors, stopped/continued state, resource usage, and complete POSIX
  job-control semantics remain unsupported. `WNOHANG` returns `0` when a matching
  child exists but no matching zombie is currently reapable.
- `SYS_FCNTL` (number=48): bounded fd-control entry for `F_GETFD`, `F_SETFD`
  with `FD_CLOEXEC`, and `F_DUPFD`. `F_DUPFD` returns the lowest available fd at
  or above the caller's minimum and clears close-on-exec on the new descriptor.
  It does not implement record locking, nonblocking I/O, async I/O, descriptor
  passing, or complete POSIX `fcntl(2)`.
- `SYS_ACCESS` (number=49): bounded path visibility/permission check through
  shared VFS path resolution and metadata. It supports only `F_OK`, `R_OK`,
  `W_OK`, and `X_OK` bits, rejects unsupported bits deterministically, and does
  not open or publish a descriptor.
- `SYS_TRUNCATE` (number=50): bounded path truncate over the same writable `/rw`
  regular-file semantics as `SYS_FTRUNCATE`. Read-only backend targets,
  directories, missing paths, invalid paths, oversized lengths, and nonblocking
  context fail without publishing a partial size update.
- `SYS_UTIMENS` (number=54): bounded path timestamp update. ABI: `rdi` = user
  path, `rsi` = atime seconds, `rdx` = mtime seconds, `r10` = BigOS flags for
  atime/mtime NOW or OMIT. It only targets supported writable `/rw` objects,
  updates ctime to the current bounded wall-clock second on success, rejects
  unsupported flag combinations and read-only backends deterministically, and
  does not implement POSIX `utimensat`, `futimens`, symlink timestamp mutation,
  nanosecond precision, timezone conversion, or directory-fd relative paths.

These lifecycle calls are bounded BigOS operations, not full POSIX `munmap` or
`mprotect`, full POSIX file-size management, or complete POSIX file timestamp
management. They do not support arbitrary byte granularity for VM operations,
`MAP_FIXED` overwrite, shared writable mappings, file-backed writable upgrades,
sparse-file APIs, journaling, power-loss recovery, swap, or cross-CPU TLB
shootdown.

The minimal user-visible socket interface begins with UDP datagram calls at
numbers 55..58 and appends the bounded TCP stream subset at numbers 62..66:

- `SYS_SOCKET` (number=55): ABI `rdi` = domain, `rsi` = type, `rdx` = protocol.
  It accepts the bounded BigOS UDP subset (`SOCKET_AF_INET`,
  `SOCKET_SOCK_DGRAM`, protocol `0`/`SOCKET_IPPROTO_UDP`) and the bounded TCP
  subset (`SOCKET_AF_INET`, `SOCKET_SOCK_STREAM`, protocol
  `0`/`SOCKET_IPPROTO_TCP`). UDP creates a datagram socket backend; TCP creates a
  stream socket backend. Both are `vfs::File` objects over the single
  kernel-internal default network context, install into the fd table, and return
  a process-local fd or deterministic negative errno
  (`-EINVAL`/`-ENODEV`/`-ENOMEM`/`-EMFILE`). Socket fds reuse the existing
  `close`/`dup`/`dup2`/`fcntl`/`close-on-exec`/`fork` paths.
- `SYS_BIND` (number=56): ABI `rdi` = socket fd, `rsi` = user
  `struct SockAddrIn*`, `rdx` = addrlen. `addrlen` MUST equal
  `sizeof(SockAddrIn)` and `family` MUST be `SOCKET_AF_INET`. It binds the local
  port through the kernel-internal UDP API and maps protocol results
  (`AlreadyBound`/`TableFull`/`InvalidArgument`) to deterministic errno.
- `SYS_SENDTO` (number=57): ABI `rdi` = fd, `rsi` = user buffer, `rdx` = length,
  `r10` = user `struct SockAddrIn*` destination, `r8` = addrlen. The payload is
  bounded by `SYS_IO_MAX_LEN`/`UDP_MAX_PAYLOAD`; it copies the bounded payload
  and destination through VMA-backed validation, transmits through the
  kernel-internal UDP API, and returns the byte count or a deterministic errno.
- `SYS_RECVFROM` (number=58): ABI `rdi` = fd, `rsi` = user buffer, `rdx` =
  length, `r10` = optional user `struct SockAddrIn*` source-out, `r8` = optional
  `uint32_t*` addrlen in/out. It performs a bounded `pump`-plus-poll RX advance
  with a bounded yield wait, copies one datagram's payload and source IPv4/port
  back to user space, and returns the byte count or `-EAGAIN` when no datagram
  arrives within the bounded wait. This is a bounded, non-general-POSIX blocking
  contract. Socket `read`/`write` deliberately return `-EOPNOTSUPP`; data flows
  only through `sendto`/`recvfrom`.

The bounded TCP stream subset appends these calls:

- `SYS_CONNECT` (number=62): ABI `rdi` = stream socket fd, `rsi` = user
  `SockAddrIn*`, `rdx` = addrlen. It actively opens a TCP connection to an IPv4
  host/port. Blocking fds wait through the bounded `tcp_pump` + connection wait
  queue path until `Established` or deterministic failure; nonblocking fds return
  `-EINPROGRESS` while the handshake is pending. Repeated calls return
  `-EISCONN` or `-EALREADY` as appropriate.
- `SYS_LISTEN` (number=63): ABI `rdi` = bound stream socket fd, `rsi` =
  backlog. It creates a protocol `LISTEN` TCB; backlog is clamped to the
  compile-time `STREAM_ACCEPT_QUEUE_CAPACITY`.
- `SYS_ACCEPT` (number=64): ABI `rdi` = listening stream socket fd, `rsi` =
  optional user `SockAddrIn*` peer-out, `rdx` = optional `uint32_t*` addrlen
  in/out. It takes one `Established` child from the listener accept queue,
  publishes it under a fresh stream socket fd, and writes the peer address when
  requested. Nonblocking fds return `-EAGAIN` when no completed connection is
  queued.
- `SYS_GETSOCKOPT` (number=65): ABI `rdi` = fd, `rsi` = level, `rdx` = optname,
  `r10` = optval, `r8` = optlen in/out. It only supports
  `SOL_SOCKET`/`SO_ERROR`, returning and clearing the connection pending error
  used by nonblocking connect completion. Other level/option pairs return
  `-ENOPROTOOPT`.
- `SYS_SEND` (number=66): ABI `rdi` = stream socket fd, `rsi` = user buffer,
  `rdx` = length, `r10` = flags. The data path is the same as `write`; the only
  accepted flag is `MSG_NOSIGNAL`, which suppresses `SIGPIPE` on a broken-pipe
  write while still returning `-EPIPE`. Unknown flag bits return `-EINVAL`.

These socket calls are the current adapters over the kernel-internal protocol
path. They do not yet provide a complete POSIX socket layer: `setsockopt`,
`shutdown`, `getpeername`, `getsockname`, `accept4`, `sendmsg`/`recvmsg`,
`SO_REUSEADDR`, scatter-gather, ancillary data, a full `AF_*`/`SOCK_*` matrix,
DHCP, DNS integration, IPv6, and multi-context/multi-NIC selection remain staged
socket/network compatibility work. `getsockopt` is currently limited to
`SOL_SOCKET`/`SO_ERROR`, and stream `send` currently recognizes only
`MSG_NOSIGNAL`.

The syscall dispatcher keeps exception/IRQ/syscall EOI separation unchanged. CPU exceptions and external IRQs remain nonblocking contexts. fd/VFS syscalls check `sched::can_block()` before allocation or synchronous ATA PIO/exFAT reads; ordinary user process syscalls can pass that guard because the DPL=3 trap gate preserves IF.

Userland raw syscall primitives `syscall0` through `syscall6` remain
BigOS-specific low-level helpers. They bind the number and return value to `rax`,
arguments to `rdi`, `rsi`, `rdx`, `r10`, `r8`, and `r9`, and list `rcx`, `r11`,
and `memory` clobbers. A source-level contract test checks these constraints so
wrapper edits cannot silently drift from this ABI; higher-level libc wrappers
remain responsible for translating negative kernel returns into positive
`errno` plus the documented failure sentinel.

## Validation: Default-Off Build Switches And Deterministic Markers

The default-off xmake option `syscall_smoke` (`xmake f --syscall_smoke=y`) continues to validate `SYS_DEBUG_WRITE`, `SYS_GET_TICK`, and unknown numbers from ring0. Additional default-off smokes cover the flat first user program, filesystem-backed user ELF, demand paging, the bounded read-only file-backed mapping (`xmake f --file_backed_mapping_smoke=y`, marker `BIGOS_FILE_BACKED_MAPPING_PASSED`/`FAILED`), anonymous map/unmap/protect lifecycle (`xmake f --anonymous_lifecycle_smoke=y`, marker `BIGOS_ANON_LIFECYCLE_PASSED`/`FAILED`), fork/COW, time/identity, signals, writable FS, pipes, and userland runtime. The userland runtime smoke also asserts representative process-group/session and foreground-terminal wrapper behavior. Normal boot now packages `/boot/user/init.elf`, enters resident PID-1 init, and starts `/bin/sh`; default headless validation observes `BIGOS_USER_EXEC`.

## Current Stage Boundaries

- Do not switch to the `syscall`/`sysret` MSR fast path.
- Do not reinterpret the current syscall set as already-complete POSIX-wide syscall semantics, user threads, job control, dynamic linking, or a full libc. These remain staged compatibility targets unless a later roadmap item explicitly excludes one.
- Do not broaden demand paging/COW beyond the current bounded anonymous mappings and the bounded read-only file-backed mapping in this stage, and do not add writable/write-back or shared file-backed `mmap` in this stage.
- Do not relax DPL for IDT gates other than syscall; do not send i8259 EOI from the syscall path.

## Cross-Cutting Engineering Items

This change did not modify `tools.bigosdev`. If later work needs it to inject `syscall_smoke` automatically and observe `BIGOS_SYSCALL_*` markers, that should be a separate cross-cutting engineering item rather than mixed into this change unless task scope is explicitly extended.
