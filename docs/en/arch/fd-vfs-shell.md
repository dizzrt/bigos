# File Descriptors And VFS Shell

BigOS stage 13 introduced a minimal read-only fd/VFS boundary. Later stages keep
that exFAT read path intact while adding bounded VMA metadata, `brk`, restricted
anonymous mapping, demand paging, writable `/rw`, page/buffer cache, pipe/dup,
and the minimal userland runtime.

## VFS Boundary

- `include/bigos/fs/vfs.h` defines the public shell: `Vnode`, `File`,
  `FileOperations`, read-only open flags, deterministic status codes, and
  `init`/`open_absolute`/`read`/`release`.
- `src/kernel/fs/vfs.cc` owns one root mount. `vfs::init()` initializes the ATA
  PIO primary-master block device, discovers the existing MBR exFAT partition,
  mounts it read-only, and publishes the root only after the mount succeeds.
- The exFAT backend is an adapter over `find_exfat_partition`, `mount_exfat`,
  `lookup`, and `read_file`; it does not rewrite the parser or change on-disk
  support.
- `open_absolute` accepts only bounded NUL-terminated absolute paths and
  read-only flags. Relative paths, `.`/`..` components, directories, missing
  files, and write/create/truncate flags fail deterministically.
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
- Exit and fault paths defer fd-backed object destruction to
  `reap_pending_processes()`, after active-stack and active-CR3 checks pass.

## Syscall ABI

- `SYS_OPEN = 5`: `rdi=path`, `rsi=flags`, returns a process-local fd or a
  negative deterministic error.
- `SYS_READ = 6`: `rdi=fd`, `rsi=user_buffer`, `rdx=len`, copies through a
  bounded kernel buffer and returns the byte count or a negative error.
- `SYS_CLOSE = 7`: `rdi=fd`, removes the descriptor and drops the file reference.
- fd/VFS syscalls check `sched::can_block()` before initializing VFS, allocating
  file objects, or entering synchronous exFAT/ATA PIO reads.
- The `int 0x80` vector and register ABI are unchanged. The syscall gate is a
  DPL=3 trap gate so ordinary process syscalls preserve IF and can pass the
  blocking guard; CPU exceptions and external IRQs remain nonblocking contexts
  with unchanged EOI rules.

## Validation

- Source-level checks cover VFS root publication, open rejection, fd capacity,
  bad fd and double-close behavior, EOF clamp, offset advancement, exec
  close-on-exec handling, and safe reaper close-all.
- The existing `fs_smoke` case now validates `/boot/fs_smoke.txt` through
  VFS open/read/release and emits the existing
  `BIGOS_FS_EXFAT_READ_PASSED` marker.
- `user_elf_smoke` reads `/boot/user/init.elf` through VFS before handing the
  bounded image to the existing ELF process loader.

## Non-Goals

- This stage did not introduce writable files, directory mutation, permissions,
  cwd, relative path resolution, `dup`, `pipe`, `select`, `lseek`, `stat`, page
  cache, demand paging, COW, user-space libc, SMP, or a UEFI backend; those are
  either later bounded capabilities or still non-goals.
- Current project non-goals remain async I/O, broad or file-backed `mmap`, full
  POSIX filesystem/process semantics, dynamic linking, SMP, and a runnable UEFI
  backend.
