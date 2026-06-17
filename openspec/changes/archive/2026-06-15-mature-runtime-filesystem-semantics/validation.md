## runtime filesystem maturity Implementation Notes

### Contract Inventory

- `kernel/core/fs`: `vfs::resolve_path()` is the shared cwd-relative normalizer for `open`, `stat`, `mkdir`, `unlink`, `rename`, and directory enumeration setup through directory fd `open`; `vfs::open_absolute()` dispatches `/rw` to `bigfs` and read-only exFAT otherwise.
- `kernel/core/fs`: `bigfs` remains a RAM-backed current-session backend mounted at `/rw`; it uses bounded inode, data-block, direct-block, directory-slot, and block-cache capacities and does not touch the existing MBR/exFAT boot image.
- `kernel/core/syscall`: fd/path syscalls copy bounded user paths or stage bounded user buffers, check `sched::can_block()` before blockable filesystem work, and pass VFS negative errno values through the syscall ABI.
- `kernel/core/proc`: process fd tables retain shared `vfs::File` objects for `dup`, `dup2`, `fork`, and exec-inherited descriptors; release happens on close, close-on-exec, exit/reap, and failed fd installation.
- `user/libc`: wrappers translate negative syscall returns into positive `errno`; `opendir`/`readdir` expose bounded directory entries, with `readdir()` using `NULL` for both EOF and error as documented by the caller-side `errno` convention.
- `user/bin` and `user/sh`: `cat`, `ls`, `mkdir`, `rm`, `rename`, `stat`, and shell redirection/cd errors report deterministic libc errno text or numeric `errno` without parsing internal backend names.

### Preserved Boundaries

- `/rw` remains RAM-backed and freshly initialized for the current boot session.
- runtime filesystem maturity does not modify MBR layout, exFAT boot assets, raw image packaging, boot addresses, linker addresses, page-table layout, IDT layout, or the `int 0x80` syscall vector.
- `fsync` is a current-runtime cache/backend consistency operation only; it does not imply cross-reboot persistence, journaling, replay, fsck, disk-backed writable partitions, or stable inode identity.
- Directory enumeration promises stable backend order only: `/rw` directory slot order and exFAT backend traversal order. It does not promise lexicographic order, POSIX cookies, complete snapshots, or full `DIR*` compatibility.

### User-Observable Backend Matrix

| Operation | read-only exFAT | RAM-backed `/rw` |
| --- | --- | --- |
| `open` | Read-only opens succeed for supported files/directories; create/write/truncate requests return `-EROFS`. | Read/write/create/truncate opens follow owner/mode, type, path, and capacity checks. |
| `read` | Reads regular files and advances offset only on success. | Reads regular files, including still-open unlinked or renamed files, and advances offset only on success. |
| `write` | Rejected with `-EROFS` through the read-only backend dispatch. | Writes preflight size and block capacity, stage block data, publish inode metadata on success, and do not advance offset on failure. |
| `lseek` | Uses bounded VFS offset arithmetic; invalid whence or negative result fails. | Uses the same VFS offset arithmetic for regular files and directories. |
| `fsync` | No-op success for read-only file objects. | Flushes dirty cache blocks to the RAM-backed block device for current-session reread after eviction. |
| `stat` | Reports bounded root-owned read-only metadata and object id `0`. | Reports current runtime type, size, mode, uid, gid, and object id `0`; removed paths fail after unlink/rename. |
| `fstat` | Reports metadata captured for the open exFAT object. | Reports live open file metadata until the final fd reference closes, including after unlink/rename. |
| `readdir` | Directory fd enumeration preserves exFAT backend traversal order. | Directory fd enumeration returns bounded name/type records in directory slot order. |
| `mkdir` | Rejected with `-EROFS`. | Creates directories after parent write permission and capacity checks. |
| `unlink` | Rejected with `-EROFS`. | Removes regular-file directory entries; open fd references remain valid until close. |
| `rename` | Any source or destination outside `/rw` is rejected with `-EROFS`. | Restricted regular-file rename succeeds within the writable backend when target is absent and permissions/capacity allow. |

### Implemented Fixes

- Added `ENOTEMPTY` to kernel errno, user errno, libc `strerror`, and VFS status mapping so non-empty directory failures no longer collapse to `-EINVAL`.
- Reordered `/rw` `O_TRUNC` so zero-size inode metadata is committed before old data blocks are freed; failed inode publication leaves the old file state explainable.
- Reordered `/rw` directory growth in `dir_reserve_slot()` and `dir_add_entry()` so cache-block availability is checked before publishing the new directory block in the parent inode, and store failures free the reserved block without mutating the caller-visible directory inode.
- Reordered cross-directory `/rw` rename setup so the source slot is pinned before destination slot reservation/growth; destination reservation failure releases the source pin without publishing a target entry.
- Made `bigfs::read()` accept zero-length reads with a null destination consistently with the VFS read contract.
- Raised read-from-user buffer validation and copying to the bounded `SYS_IO_MAX_LEN` path so file/pipe writes can use the documented fd I/O bound while console writes remain limited by `SYS_WRITE_MAX_LEN`.
- Changed `sys_pipe()` to validate the fd output array with the writable user-buffer validator before copying descriptors back to user space.
- Added userland filesystem assertions for regular-file directory enumeration rejection (`-ENOTDIR`) and invalid read/stat output buffers (`-EFAULT`).
- Added a default-off `filesystem_maturity_smoke` matrix case that reuses the broad userland filesystem assertions and emits `BIGOS_FILESYSTEM_MATURITY_PASSED`.

### Validation Notes

- Source-level check added: `tests/test_runtime_filesystem_maturity_source.py`.
- Documentation updated in both language mirrors: `docs/en/arch/runtime-smoke-validation.md`, `docs/zh/arch/runtime-smoke-validation.md`, `docs/en/arch/writable-fs-page-cache-pipe.md`, and `docs/zh/arch/writable-fs-page-cache-pipe.md`.
- Passed: `uv run pytest tests/test_runtime_filesystem_maturity_source.py tests/test_writable_fs_page_cache_pipe_source.py -q` (`18 passed`).
- Passed: `openspec validate mature-runtime-filesystem-semantics --strict`.
- Passed: `xmake f --filesystem_maturity_smoke=y && xmake`, confirming the x86_64 cross-toolchain is available for this build.
- Passed: `clang++ -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -target x86_64-elf -Iinclude -Icpp/include -Icpp/libsupc++/include -Ikernel -nostdinc++ -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/proc/proc.cc kernel/core/syscall/syscall.cc`.
- Passed: `uv run python tools/boot_debug.py runtime-smoke-matrix --case filesystem-maturity --output build/test/filesystem-maturity-validation.md`, observed `BIGOS_FILESYSTEM_MATURITY_PASSED` via QEMU headless serial log.
- Passed: `uv run python tools/boot_debug.py runtime-smoke-matrix --case filesystem-writable --output build/test/filesystem-writable-validation.md`, observed `BIGOS_WRITABLE_FS_PASSED` via QEMU headless serial log.
- Skipped: Bochs cross-validation was not run in this pass; QEMU headless plus source, clang syntax, xmake build, and OpenSpec validation are the substitute checks. Residual risk is limited to Bochs-specific BIOS/IDE/display behavior outside the runtime filesystem maturity code changes.
