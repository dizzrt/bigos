# File Descriptors And VFS Shell

BigOS stage 13 introduced a minimal read-only fd/VFS boundary. Later stages keep
that exFAT read path intact while adding bounded VMA metadata, `brk`, restricted
anonymous mapping, demand paging, writable `/rw`, page/buffer cache, pipe/dup,
and the minimal userland runtime.

## VFS Boundary

- `include/bigos/fs/vfs.h` defines the public shell: `Vnode`, `File`,
  `FileOperations`, open flags, deterministic status codes, cwd-aware path
  resolution, and `init`/`open`/`open_absolute`/`read`/`release`.
- `kernel/core/fs/vfs.cc` owns one root mount. `vfs::init()` initializes the ATA
  PIO primary-master block device, discovers the existing MBR exFAT partition,
  mounts it read-only, and publishes the root only after the mount succeeds.
- The exFAT backend is an adapter over `find_exfat_partition`, `mount_exfat`,
  `lookup`, and `read_file`; it does not rewrite the parser or change on-disk
  support.
- Path-taking VFS entry points share a bounded resolver. Absolute paths resolve
  from root; relative paths resolve from the current process cwd; repeated
  separators are collapsed; POSIX-style `.` and `..` components are reduced; and
  root's parent remains root.
- The resolver does not implement symlink traversal, mount namespaces, `chroot`,
  or full POSIX `realpath` canonicalization. Empty, overlong, or unsupported
  paths fail deterministically before fd or filesystem state is published.
- `read` uses the open file offset, clamps at EOF, advances the offset only after
  a successful backend read, and rejects offset arithmetic overflow.

## Process FD Table

- Each `Process` owns a growable descriptor table (a heap-allocated `FdEntry`
  array bounded by `MAX_FDS_SOFT_LIMIT` rather than a fixed inline size), with
  entries pointing at VFS `File` objects. The storage is allocated lazily and
  freed when the process is reaped.
- `open` installs the lowest available fd in the current process table, growing
  the table on demand; reaching the fd soft limit or a failed growth allocation
  returns a deterministic `-bigos::EMFILE` error and drops the unpublished file
  reference.
- `read` and `close` reject out-of-range, unused, already-closed, and
  non-readable descriptors with a deterministic bad-fd error.
- `exec` preserves descriptors unless their internal `close_on_exec` bit is set;
  rollback leaves the old fd table untouched.
- Each user `Process` also owns an inline bounded cwd string. It initializes to
  `/`, is copied independently by `fork`, is preserved by `execve`, and requires
  no heap teardown during safe reap.
- Exit and fault paths defer fd-backed object destruction to
  `reap_pending_processes()`, after active-stack and active-CR3 checks pass.

## Syscall ABI

- `SYS_OPEN = 5`: `rdi=path`, `rsi=flags`, returns a process-local fd or a
  negative deterministic error.
- `SYS_READ = 6`: `rdi=fd`, `rsi=user_buffer`, `rdx=len`, copies through a
  bounded kernel buffer and returns the byte count or a negative error.
- `SYS_CLOSE = 7`: `rdi=fd`, removes the descriptor and drops the file reference.
- `SYS_STAT = 29`: `rdi=path`, `rsi=struct stat*`, returns a bounded BigOS
  metadata snapshot for an absolute path.
- `SYS_FSTAT = 30`: `rdi=fd`, `rsi=struct stat*`, returns metadata for the open
  file object without advancing its offset.
- `SYS_CHDIR = 31`: `rdi=path`, resolves the target, verifies it is a directory,
  and commits the new cwd only on success.
- `SYS_GETCWD = 32`: `rdi=user_buffer`, `rsi=len`, copies a NUL-terminated cwd
  string or returns `-ERANGE` when the valid buffer is too small.
- fd/VFS syscalls check `sched::can_block()` before initializing VFS, allocating
  file objects, or entering synchronous exFAT/ATA PIO reads.
- The `int 0x80` vector and register ABI are unchanged. The syscall gate is a
  DPL=3 trap gate so ordinary process syscalls preserve IF and can pass the
  blocking guard; CPU exceptions and external IRQs remain nonblocking contexts
  with unchanged EOI rules.

## Validation

- Source-level checks cover VFS root publication, cwd resolution, open
  rejection, fd capacity, bad fd and double-close behavior, EOF clamp, offset
  advancement, metadata snapshots, exec close-on-exec handling, and safe reaper
  close-all.
- The existing `fs_smoke` case now validates `/boot/fs_smoke.txt` through
  VFS open/read/release and emits the existing
  `BIGOS_FS_EXFAT_READ_PASSED` marker.
- `user_elf_smoke` reads `/boot/user/init.elf` through VFS before handing the
  bounded image to the existing ELF process loader.

## Bounded Metadata

- BigOS exposes a small metadata structure through `stat`/`fstat` wrappers and a
  packaged `/bin/stat` observer. It contains object type, size, mode, uid, gid, a
  bounded link-count default, a user-visible object id that is zero in this ABI
  version, and reserved zero fields.
- exFAT metadata is read-only and reports documented defaults for owner and
  mode. `/rw` metadata reflects successful runtime create, write, truncate,
  mkdir, unlink, and permission metadata changes while remaining RAM-backed and
  non-persistent across reboot.
- This is a BigOS bounded metadata subset, not complete POSIX `struct stat`,
  device-node, symlink, ACL, xattr, complete timestamp, stable inode, or
  persistent object identity semantics.

## Non-Goals

- This baseline still does not introduce `select`, complete POSIX `stat`, full
  pathname canonicalization, symlink traversal, mount namespaces, `chroot`, SMP,
  or a UEFI backend; those are either later bounded capabilities or still
  non-goals.
- Current project non-goals remain async I/O, broad or file-backed `mmap`, full
  POSIX filesystem/process semantics, dynamic linking, SMP, and a runnable UEFI
  backend.
