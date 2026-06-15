## Why

Stage 41 needs to turn the existing bounded filesystem pieces into a coherent runtime filesystem contract that ordinary static user programs can rely on. BigOS already has read-only exFAT boot assets, RAM-backed `/rw`, fd/VFS operations, metadata queries, current-directory relative paths, and small user tools, but their combined behavior still needs a focused maturity pass before future persistent writable storage can be considered safely.

## What Changes

- Harden runtime filesystem semantics across read-only exFAT and RAM-backed `/rw`, including create/write/read/lseek/fsync/stat/list/unlink/restricted rename combinations.
- Make directory behavior, metadata visibility, permissions, capacity failures, open-file references, and errno mapping stable across path-taking and fd-taking operations.
- Preserve the current x86_64 Legacy BIOS/MBR/exFAT delivery baseline and keep `/rw` as a current-boot-session RAM-backed writable filesystem.
- Add behavior-oriented validation for success and failure paths that are visible to simple C programs, libc wrappers, shell commands, and runtime smoke paths.
- Explicitly keep cross-reboot persistence, disk-backed writable partitions, journaling, broad storage drivers, async I/O, symlinks, hard links, mount namespaces, broad file-backed `mmap`, and complete POSIX filesystem compatibility out of scope.

## Capabilities

### New Capabilities

- `runtime-filesystem-maturity`: Defines the Stage 41 cross-capability contract for ordinary-program filesystem behavior, current-runtime consistency, bounded failure semantics, and persistent-storage preparation boundaries.

### Modified Capabilities

- `fd-vfs-shell`: Clarifies combined path/fd runtime operation behavior, backend dispatch, reference lifecycle, directory fd handling, and stable errno mapping.
- `writable-filesystem`: Strengthens `/rw` runtime consistency, capacity/permission failure atomicity, open-file reference behavior after unlink/rename, and non-persistent RAM-backed boundaries.
- `file-metadata-contract`: Requires metadata to reflect successful runtime file and directory changes while preserving path-vs-fd distinctions after unlink/rename.
- `current-directory-relative-paths`: Clarifies that Stage 41 filesystem operations consistently share cwd-relative path resolution without adding namespace, symlink, or full canonicalization semantics.
- `runtime-smoke-validation`: Extends behavior validation expectations for Stage 41 filesystem maturity while recording skipped emulator/toolchain dependencies.

## Impact

- Affected subsystems: `kernel/core/fs`, `kernel/core/syscall`, `kernel/core/proc` fd lifecycle integration, `user/libc`, `/bin` user tools, shell-visible error reporting, and runtime smoke validation.
- Architecture assumptions: current runnable target remains x86_64 with the Legacy BIOS/MBR/exFAT path as the default baseline; no new ISA backend is introduced.
- Disk layout assumptions: existing boot image, MBR, exFAT boot assets, and read-only exFAT discovery remain unchanged; `/rw` remains RAM-backed and is not persisted across reboot.
- Memory/context assumptions: filesystem operations that allocate, block, issue synchronous block I/O, flush cache state, or mutate directory entries run only from safe process context.
- Toolchain/emulator assumptions: validation may use xmake, the x86_64-elf cross toolchain, QEMU/Bochs, and serial markers when available; missing local dependencies are recorded as skipped validation rather than passed.
