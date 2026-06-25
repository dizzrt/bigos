# Writable Filesystem, Page/Buffer Cache, And Pipes

BigOS phase 18 adds the first writable I/O foundation on top of the existing
read-only stack (read-only ATA PIO, read-only exFAT mount, read-only fd/VFS
shell). It introduces a kernel block buffer cache, a block-device write path, a
minimal writable filesystem (`bigfs`), and `pipe`/`dup`/`dup2`. The owner/mode
permission primitive `cred::may_access` (phase 16.5) becomes an actual
enforcement point on the writable path. Nothing on the read-only exFAT path, the
disk image, MBR/partition discovery, the `int 0x80` register ABI, IDT/vector
layout, DPL settings, page-table self-mapping, or CR3 conventions changes.

## Block Buffer Cache

`bigos::bcache` (`include/bigos/fs/bcache.h`, `kernel/core/fs/bcache.cc`) caches
fixed-size blocks keyed by `(BlockDevice*, block_no)`. The block size is one
sector (512 bytes) and the capacity is a bounded compile-time constant
(`CACHE_BLOCKS`). Backing pages are allocated once via `alloc_kernel_pages` with
`_GFM_PRE_PAGING`.

- `get(dev, block_no)` returns a pinned (`ref_count`-incremented) block; a hit
  returns without re-reading, a miss loads through `bigos::block_io::read_sync`.
- `put`, `mark_dirty`, `sync`, and `sync_all` release a reference, flag a dirty
  block, and write dirty blocks back through `bigos::block_io::write_sync`.
- `sync_block(dev, block_no)` lets filesystem code flush selected dirty blocks
  in an explicit order. Missing or clean selected blocks are success; failed
  writes keep the cached block dirty or pending.
- Write-back semantics: a write only marks the block dirty; the durable point is
  `fsync`, eviction write-back, or `sync_all`.
- Eviction prefers an unreferenced clean block (no flush); the only evictable
  candidate being dirty is written back first. When every slot is pinned `get`
  returns `nullptr` (the caller maps that to `-ENOMEM`/`-ENOSPC`); it never busy
  waits and never flushes from IRQ context. Forced device invalidation also
  preserves dirty blocks when write-back fails instead of dropping the cache
  slot as durable.
- Load and write-back perform synchronous block I/O and MUST run only in
  blockable process context; the syscall layer checks the scheduler blocking
  guard before entering. On a device write error the block stays dirty (no data
  loss) and `IoError` is returned.

## Block Device Write Path

`BlockDevice` gains an appended `write_impl` field (a null `write_impl` marks a
read-only device) and `block::write_sectors(dev, lba, count, src, src_len)`,
symmetric to `read_sectors`. It validates the sector count, source length, and
LBA-range overflow before dispatching. The ATA PIO backend implements LBA48
WRITE SECTORS EXT plus FLUSH CACHE EXT, reusing the existing BSY/DRDY/DRQ polling
timing. A read-only device returns `Unsupported`, mapped to `-EROFS` upstream.

## Block I/O Request Layer

`bigos::block_io` adds a bounded synchronous request layer between normal
storage consumers and the low-level block execution API. Requests record the
target `BlockDevice`, read/write operation, LBA, sector count, kernel buffer,
buffer length, queue slot, and final completion status. Each device has a fixed
small queue; invalid requests, exhausted queues, preemption-disabled callers, and
device-not-ready cases return deterministic request-layer statuses before any
device I/O is issued.

The current contract is synchronous only. It does not provide async callbacks,
background writeback, DMA, multi-queue scheduling, SMP I/O dispatch, new storage
drivers, user-visible device nodes, or a new syscall ABI. Low-level
`block::read_sectors` and `block::write_sectors` remain the request layer's
execution interface and tightly bounded hardware diagnostic boundary.

## Writable Filesystem (`bigfs`)

`bigos::bigfs` (`include/bigos/fs/bigfs.h`, `kernel/core/fs/bigfs.cc`) is a
minimal writable filesystem mounted at `/rw`, coexisting with the read-only
exFAT mount. Its default backing medium is a RAM-backed `BlockDevice` (decision
9), so the whole write path runs end to end without touching the on-disk image.
Layout (in 512-byte blocks): superblock, inode bitmap, data bitmap, inode table,
then a data region. It is bounded throughout: fixed inode count, direct-block-
only files (bounded `MAX_FILE_SIZE`), and fixed directory entry size. All
metadata and data go through the block buffer cache.

Each inode carries `owner` (uid/gid) and `mode`. Supported operations: writable
`open` (`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`), file `write`, `lseek`,
`O_TRUNC` truncation, `mkdir`, minimal directory enumeration, `unlink`, and
restricted regular-file `rename` within the writable backend.
`unlink` removes the directory entry first; open file descriptors keep the inode
and data blocks alive until the last reference closes, and a pre-rename fd
continues to reference the same runtime file after a successful rename. Failure semantics are
deterministic (`-ENOSPC`/`-EEXIST`/`-ENOENT`/`-ENOTDIR`/`-EISDIR`/`-EINVAL`/
`-ENOTEMPTY`/`-EIO`/`-EACCES`/`-EROFS`) and never publish half-written metadata.

By default the medium is RAM-backed and is not persistent across reboots; that
mode only guarantees current-runtime consistency (write-then-read-back, metadata
and directory enumeration visibility, and read-back after `fsync` plus cache
eviction).

`BIGOS_PERSISTENT_WRITABLE_FS` selects an optional persistent `/rw` backend over
an independent test disk. By default that persistent test disk is the ATA
primary-slave role; the default-off `BIGOS_PERSISTENT_WRITABLE_FS_MODERN_BACKEND`
configuration instead selects the internal modern virtio-blk validation role.
Both choices keep all `/rw` I/O flowing through VFS, the page/buffer cache, and
the block request layer; there are no modern-driver private filesystem hooks,
device nodes, mount ABI, or default boot replacement. The persistent layout keeps
the same bounded BigFS limits and adds an explicit superblock
magic/version/block-size/capacity/root metadata checksum. Normal boot mounts an
existing compatible volume only after recognition and bounded metadata validation
succeed; invalid magic, unsupported version, invalid capacity, inode or
directory-entry bounds errors, block mapping conflicts, or data-bitmap ownership
contradictions fall back to RAM-backed `/rw` for the default ATA configuration
without auto-formatting, auto-repair, or migration. Persistent metadata updates
use a bounded ordered commit unit over selected cache blocks: newly initialized
data or directory blocks are synchronized before inode or directory metadata that
publishes them, and inode references are synchronized before released blocks are
made durably reusable. The bounded `/bin/mkfs_bigfs` tool invokes the
BigOS-specific explicit format hook for the configured persistent test disk; it
is not a POSIX `mkfs`, `mount`, or device-management tool. Persistent mode only
promises clean-sync plus clean reboot visibility for successful
`fsync`/write-back state. Failed metadata write-back returns a deterministic
error and leaves dirty/pending cache state; it does not report durable success.
There is no journaling, crash recovery, async I/O, broad storage driver support,
stable inode identity, full POSIX `DIR*`, or power-loss consistency guarantee.

## Permission Enforcement

`bigfs` open (write/create), `write`, `mkdir`, and `unlink` call
`cred::permits(file_uid, file_gid, mode, req_uid, req_gid, access)` before
mutating state: root is always allowed, otherwise owner/group/other bits apply.
A denial returns `-EACCES` without changing filesystem state. A new file's owner
is the calling process identity and its mode is the caller-supplied value. The
read-only exFAT backend rejects any write/create request with `-EROFS`. The
decision logic itself is unchanged from phase 16.5.

## VFS And fd Extensions

`FileOperations` gains appended `write`, `lseek`, `truncate`, and minimal
`readdir` ops and `File` gains an appended `writable` flag (the read-only
`read`/`close` layout is preserved). A backend with a null `write` op is
read-only (`write` returns `-EROFS`); a null `lseek` op uses ordinary offset
arithmetic with overflow checks. `open_absolute` has a writable overload
accepting create flags plus `O_CREAT` mode/owner; read-only opens of `/rw`
directories produce directory fds that can enumerate bounded name/type records.
The fd layer adds `dup`/`dup2` (sharing the same `File` and its offset,
retaining once per new fd; `dup2` closes an already-open target first), and
`write`/`lseek`/`fsync`/bounded `ftruncate`/minimal directory enumeration through
a process-local fd. VFS also exposes a bounded active writable-backend `sync`
entry that flushes pending metadata and dirty cache blocks through the
device-scoped cache path; it is not full POSIX `sync(2)`, `fdatasync`, async
write-back, mount namespace synchronization, journaling, crash recovery, or
power-loss recovery. The libc `truncate(path, len)` wrapper is implemented as a
bounded open + `ftruncate` sequence, not a complete POSIX pathname API.

For `/rw` regular files, extension writes can append, cross cache blocks, or
seek past EOF within `MAX_FILE_SIZE`. Reads from newly exposed gaps return zero
bytes until overwritten. Shrinking truncate publishes the new size before
released tail blocks re-enter the free set; extending truncate exposes a
zero-read range without promising full sparse-file or hole-preservation APIs.
Reused blocks are zeroed or completely staged before user-visible reads can see
them.

## Pipes

`bigos::ipc` (`include/bigos/ipc/pipe.h`, `kernel/core/ipc/pipe.cc`) provides a
bounded ring-buffer pipe with a connected read-end and write-end `File`. Reads
block on an empty buffer while writers remain open and are woken on write; writes
block on a full buffer while readers remain open and are woken on read. Blocking
happens only in blockable process context; a non-blockable context fails
deterministically. When all write ends close, reads return 0 (EOF); when all read
ends close, writes return `-EPIPE` (`SIGPIPE` delivery is an optional
enhancement, decision 10). `lseek` on a pipe returns `-ESPIPE`. End reference
counts are tracked precisely: `fork` inherits ends and increments counts, `exec`
honors close-on-exec, and exit/reap closes every remaining end exactly once,
reclaiming the pipe when both ends are gone.

## Syscall ABI

New numbers are appended after `SYS_SIGRETURN = 19`: `SYS_LSEEK = 20`,
`SYS_PIPE = 21`, `SYS_DUP = 22`, `SYS_DUP2 = 23`, `SYS_FSYNC = 24`,
`SYS_MKDIR = 25`, `SYS_UNLINK = 26`, `SYS_EXECVE = 27`, `SYS_READDIR = 28`,
`SYS_RMDIR = 38`, `SYS_FTRUNCATE = 39`, and `SYS_SYNC = 40`.
`SYS_OPEN = 5` is extended to accept writable/create flags and an `O_CREAT`
mode; `SYS_WRITE = 2` is extended to write file and pipe fds while preserving
the console fast path. The register ABI, existing numbers, vector layout, and
the "syscall sends no EOI" rule are unchanged. Write/pipe/FS syscalls check the
scheduler blocking guard before allocating or entering synchronous block I/O or
blocking.

## Validation Smoke

Two default-off switches gate runtime smokes; the existing smoke matrix is
unchanged.

- `xmake f --writable_fs_smoke=y` enables `BIGOS_WRITABLE_FS_SMOKE`. From a
  blockable kernel thread it covers `O_CREAT` create + write + read-back,
  append/cross-block/seek-past-EOF growth, zero gap reads, shrink/extend
  truncate, block reuse zeroing, capacity failure, `fsync` then forced cache
  eviction then consistent read-back, owner/mode permission denial, and a
  read-only backend write rejected with `-EROFS`,
  emitting `BIGOS_WRITABLE_FS_PASSED`/`BIGOS_WRITABLE_FS_FAILED`.
- `xmake f --pipe_smoke=y` enables `BIGOS_PIPE_SMOKE`, covering cross-thread FIFO
  write/read, blocking-read then write wakeup, write-end-closed EOF, and
  read-end-closed `-EPIPE`, emitting `BIGOS_PIPE_PASSED`/`BIGOS_PIPE_FAILED`.
- `xmake f --persistent_writable_fs_smoke=y` enables
  `BIGOS_PERSISTENT_WRITABLE_FS_SMOKE` and the persistent backend. The helper can
  attach an independent test disk with `--persistent-image`; the first boot
  formats/writes/fsyncs metadata consistency state, runs the bounded explicit
  writable-backend sync path, and expects
  `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`, while a second boot with the same
  persistent image expects `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`. The
  checked state includes directory entries, inode metadata, file sizes, block
  mappings, free-space bitmap effects, stable growth/truncate behavior, and
  read-only exFAT isolation.
- `xmake f --modern_storage_backend_smoke=y` enables
  `BIGOS_MODERN_STORAGE_BACKEND_SMOKE`, selecting the internal modern virtio-blk
  role and validating request-layer read/write, cache-mediated write/read
  round-trip, and writeback failure dirty-state retention. It requires an
  explicit modern virtio-blk device and remains independent of default ATA boot.
- `xmake f --persistent_writable_fs_smoke=y --persistent_writable_fs_modern_backend=y`
  runs the same bounded clean-sync persistent `/rw` smoke over the modern
  backend when the emulator supplies a compatible virtio-blk image. Success only
  covers data and metadata synchronized through cache write-back and
  request-layer terminal success; it does not claim crash consistency or
  power-loss recovery.

Run the headless QEMU serial-marker smoke with, for example:

```bash
xmake f --writable_fs_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none \
  --serial-log logs/serial.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED
```

When QEMU/Bochs, ROM/display, the cross toolchain, or the disk image are
unavailable, record the missing tool, the skipped validation, and residual risk
instead of claiming a runtime run.

## Non-Goals

- No hard/soft links, broad cross-backend or directory `rename`, full
  `stat`/`fstat`, full `fcntl`, or full `readdir`/`getdents` traversal.
- No file-backed mmap or shared page-cache mappings, multi-mount namespaces,
  `mount`/`umount`, journaling, `fsck`, quotas, or ACL/xattr.
- No named pipes (FIFO)/`mknod`/socket, and no pipe signal semantics beyond the
  optional `SIGPIPE`.
- No persistent `/rw` journaling, crash recovery, async I/O, broad storage
  drivers, general block-device management, or complete POSIX filesystem
  compatibility.
- No SMP cache coherency or write performance optimization (correctness and
  boundedness only).
- No changes to the `int 0x80` register ABI, existing syscall numbers, IDT/vector
  layout, DPL, page-table/CR3/address layout, external IRQ/exception EOI
  semantics, or the MBR/partition/exFAT read-only discovery and disk image
  layout.
