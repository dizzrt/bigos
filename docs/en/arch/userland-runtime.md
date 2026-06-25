# Userland Runtime, libc, and Shell

BigOS now has a bounded, freestanding userland path built from C sources under
`user/`. It is still a minimal research-kernel userland, not a complete POSIX
environment.

## Components

- `user/crt0/crt0.s`: user entry `_start`. It consumes the initial stack produced
  by `copy_exec_args_to_stack` in `kernel/core/proc/proc.cc`.
- `user/libc`: bounded minimal C library subset, syscall wrappers, errno
  translation, cwd wrappers, ASCII/C-locale-style `ctype`, bounded
  `time.h`/`assert.h`, string/memory helpers, `brk`-backed `malloc`/`free`,
  tiny fd-backed stdio with opaque standard streams, `printf`,
  `fprintf(stderr, ...)`, deterministic error text, and read-only
  `environ`/`getenv`.
- `user/init/init.c`: resident PID-1. It starts `/bin/sh` with
  `fork` + `execve`, waits for children, and restarts the shell when it exits.
- `user/sh/sh.c`: bounded interactive shell with builtins, cwd-aware PATH lookup,
  `fork` + `execve` + `wait`, one-stage pipes, and basic `<` / `>` redirection.
- `user/bin/*`: small packaged user binaries such as `/bin/echo`, `/bin/cat`,
  `/bin/ls`, `/bin/mkdir`, `/bin/rm`, `/bin/rename`, `/bin/stat`, and
  `/bin/pwd`.
- `user/smoke/bin/*`: validation-only C probes that are built and packaged under
  `/bin/smoke/*` only when the default-off `userland_smoke` path is selected.
- `user/smoke/userland_smoke.c`: default-off deterministic validation program
  for crt0, libc wrappers, errno, stdout/stderr, smoke C-program execution,
  fork/exec/wait, pipe, redirection, and malloc.

## crt0 Stack Contract

The kernel enters a user ELF image with `rsp` pointing at this layout:

```text
rsp -> argc
       argv[0]
       ...
       argv[argc - 1]
       NULL
       envp[0]
       ...
       NULL
       argument and environment strings
```

`_start` reads `argc` into `rdi`, `argv` into `rsi`, and `envp` into `rdx`, aligns
the stack for the System V x86_64 call boundary, calls
`main(argc, argv, envp)`, and exits through `SYS_EXIT` with `main`'s return
value. It never returns to an undefined address.

## Current Directory

Each user process owns a bounded cwd string initialized to `/`. `fork` copies it
independently, `execve` preserves it, and `chdir` commits a new cwd only after
the kernel resolves the target and verifies that it is a directory. Path-taking
wrappers pass absolute or relative paths unchanged to the kernel; libc and the
shell do not implement a separate namespace, symlink traversal, `chroot`, or
`realpath`.

The resolver supports ordinary components, repeated separators, POSIX-style `.`
and `..`, and keeps root's parent at root. `getcwd` copies the NUL-terminated cwd
into a caller buffer and reports `ERANGE` when a valid buffer is too small.

## SYS_EXECVE ABI

`SYS_EXECVE` is appended as syscall number `27`.

```text
rax = SYS_EXECVE
rdi = const char *path
rsi = char *const argv[]
rdx = char *const envp[]
```

On success it replaces the current process image and enters the new program; it
does not return. On failure it returns a negative errno such as `-ENOENT`,
`-EACCES`, `-ENOEXEC`, `-E2BIG`, `-EFAULT`, `-ENOMEM`, `-EWOULDBLOCK`, or `-EIO`.
The kernel copies `path`, `argv`, and `envp` through VMA-backed user-buffer
validation and bounded `EXEC_MAX_*` limits before reading the target ELF through
VFS.

The user libc mirror headers `user/libc/include/sys_nr.h` and
`user/libc/include/errno.h` intentionally do not include C++ kernel headers.
`tests/test_syscall_entry_source.py` asserts that their values match
`include/bigos/syscall.h` and `include/bigos/errno.h`.

## Shell Bounds

The shell is intentionally small:

- Builtins: `exit`, `echo`, and `cd`. `cd` runs in the shell process so the cwd
  change survives the next command.
- Prompt: deterministic `$ ` only when stdin and stdout are still connected to
  the default console fast paths.
- Input feedback: printable characters, newline, and backspace are echoed from
  the non-interrupt shell consumer after `read(0, ...)` returns.
- Command lookup: paths containing `/` execute directly; other names are tried
  against `PATH`, with `/bin` as the default.
- Execution: external commands run via `fork` + `execve` + `wait`.
- Pipes: one `a | b` stage.
- Foreground control: before running an external command or one-stage pipe, the
  shell places the child process or pipeline in a bounded process group, binds
  the single default terminal foreground group to it, waits, and then restores
  the shell process group. This is only BigOS foreground-command behavior.
- Redirection: one input `< file` and one output `> file` per command.
- Cwd behavior: relative command paths containing `/`, redirection paths, and
  small tools such as `/bin/pwd` use the kernel cwd contract.
- Output: builtins, child stdout, and deterministic recoverable errors use the
  existing fd/syscall path; default fd `1` and fd `2` reach the visible console
  when not redirected.
- Capacity: line length, argument count, PATH candidates, and path lengths are
  fixed upper bounds in `user/sh/sh.c`.

This does not implement complete job control, background processes, `fg`/`bg`,
job tables, globbing, variable expansion, shell scripts, sub-shells, `termios`,
multiple terminals, a full directory API, symlinks, a persistent full writable
filesystem, broad file-backed `mmap`, a full FILE API, dynamic linking, SMP, or
a complete POSIX libc.

## Minimal libc Subset

The user libc exposes a documented bounded subset for simple static C programs:

- Headers: `assert.h`, `ctype.h`, `stdio.h`, `stdlib.h`, `string.h`,
  `errno.h`, `time.h`, `unistd.h`, `fcntl.h`, `sys/types.h`, `sys/wait.h`,
  `sys/stat.h`, `bigos_dirent.h`, plus the compatibility umbrella `libc.h`.
  Raw syscall primitives are opt-in through `bigos_syscall.h`, not exported by
  the ordinary umbrella header.
- Types and constants: `size_t`, `ssize_t`, `off_t`, `pid_t`, `NULL`, the
  implemented open flags, bounded fd-control constants (`F_GETFD`, `F_SETFD`,
  `F_DUPFD`, `FD_CLOEXEC`), access mode bits, seek constants, `WAIT_ANY`,
  `WNOHANG`, and errno values mirrored from `include/bigos/errno.h`, including
  `ERANGE` for small `getcwd` buffers.
- File metadata and directory helpers: `struct stat`, `S_ISDIR`, `S_ISREG`, and
  `struct bigos_dirent` describe only the current bounded file/directory subset.
  `bigos_readdir` remains a BigOS-specific batched helper, while `DIR`,
  `struct dirent`, `opendir`, `readdir`, and `closedir` provide a bounded
  `DIR*`-style wrapper. These interfaces are not complete POSIX directory
  traversal and do not promise ordering, snapshots, `telldir`, `seekdir`,
  `rewinddir`, symlinks, directory fd semantics, or persistent full-filesystem
  semantics.
- Syscall wrappers: negative kernel errno returns become positive user `errno`
  and `-1` or the documented failure sentinel; successful wrappers do not clear
  or rewrite an existing `errno` value. POSIX-like names such as `waitpid`,
  `fcntl`, `access`, `stat`, `fstat`, `truncate`, and `ftruncate` are BigOS
  bounded subsets, not claims of complete POSIX behavior.
- BigOS-specific helpers: `wait_status`, `bigos_readdir`, `brk_raw`,
  `mmap_anon`, `time_now`, and `get_tick` are public bounded ABI helpers because
  current shell, smoke, libc, or packaged user-program paths use them. Raw
  `syscall0` through `syscall6` remain low-level BigOS ABI helpers only for
  libc internals or callers that explicitly include `bigos_syscall.h`; they do
  not translate `errno` and are not POSIX `syscall(2)` compatibility.
- `ctype`, time, and assert: `ctype.h` provides deterministic ASCII/C-locale
  classification and `toupper`/`tolower` only. `time.h` exposes second-resolution
  `time()` backed by the BigOS bounded time primitive. `assert.h` supports
  `NDEBUG`; enabled failures print a deterministic stderr diagnostic and
  terminate through the user libc exit path.
- Strings and memory: the subset includes the implemented bounded routines such
  as `strlen`, `strcmp`, `strncmp`, `memcpy`, `memset`, and overlap-safe
  `memmove`, plus the stateless search helpers `strchr`, `strrchr`, `strstr`,
  and `memchr`. It does not expose `strtok`, `qsort`, or `bsearch`. Null pointer
  inputs keep ordinary C preconditions and are not given extra BigOS-specific
  safety promises.
- Stdlib and heap: `strtol` and `strtoul` support bounded integer parsing with
  base handling, `endptr`, no-digit behavior, and `ERANGE`; `atoi` is the decimal
  convenience wrapper. `malloc`, `calloc`, and `realloc` are backed by the
  bounded brk allocator: `calloc` checks multiplication overflow and zeroes
  memory, `realloc` preserves the old allocation on failure, and `free(NULL)` is
  a no-op. The allocator does not promise thread safety, complete coalescing, or
  hosted allocator behavior.
- Stdio: `stdin`, `stdout`, and `stderr` are opaque handles for fd `0`, `1`, and
  `2` only. `putchar`, `puts`, `printf`, and `fprintf(stderr, ...)` are fd/write
  based; `snprintf` uses the same bounded formatter. The formatter supports the
  existing minimal forms plus simple width, `%u`, `%p`, `%ld`, `%lu`, and `%zu`.
  There is no `fopen`, `fclose`, full buffering, precision, locale,
  floating-point formatting, wide-character support, or hosted `FILE` semantics.
- Environment: `envp`, `environ`, and `getenv` are read-only. This stage does
  not implement `setenv`, `putenv`, or `unsetenv`.

## Simple C Program Baseline

The simple C program baseline treats simple static C programs as a user-visible compatibility
baseline, still within the existing freestanding runtime boundary:

- Entry: `_start` calls `main(argc, argv, envp)` using the existing user stack
  layout, and `main`'s return value is passed to `SYS_EXIT`.
- Wrappers: libc syscall wrappers translate negative kernel returns into
  positive `errno` values and return `-1` or the documented failure sentinel.
- Output: programs use fd-based `write`, `putchar`, `puts`, bounded
  `printf`/`fprintf`, or `snprintf`; stdout is fd `1` and deterministic errors
  can be written to fd `2`.
- Environment: `envp`, `environ`, and `getenv` are read-only. If no environment
  is supplied, programs must report that empty boundary deterministically.
- Smoke-only probes: `/bin/smoke/args`, `/bin/smoke/env`, `/bin/smoke/out`,
  `/bin/smoke/errno`, `/bin/smoke/exit`, and `/bin/smoke/libc_subset` cover
  argument handoff, environment reporting, stdout/stderr, wrapper failure plus
  `errno`, requested exit status, fine-grained libc headers,
  ASCII/C-locale `ctype`, bounded `time.h`, enabled `assert`, `strtoul`,
  stateless search helpers, `fprintf(stderr, ...)`, `snprintf`/formatter
  behavior, string/memory boundaries, `strtol`/`atoi`, `calloc`/`realloc`,
  `DIR*` wrappers, and bounded heap behavior when `userland_smoke` is enabled.

This baseline does not add kernel syscalls, change the `int 0x80` register ABI,
change boot or disk layout, introduce dynamic linking, or claim hosted libc or
full POSIX shell behavior.

## Build and Packaging

`xmake.lua` builds user C programs with the cross toolchain as static
freestanding ELF64 `ET_EXEC` images using `user/crt0`, `user/libc`, and
`user/link.lds`. The boot image packages:

- `/boot/user/init.elf` for the default resident C init or the selected smoke.
- `/bin/sh` for the interactive shell.
- `/bin/echo`, `/bin/cat`, `/bin/ls`, `/bin/mkdir`, `/bin/rm`, `/bin/rename`,
  `/bin/stat`, and `/bin/pwd` for normal packaged user commands.
- `/bin/smoke/*` probes only for the explicit `userland_smoke` validation image.

The image layout remains the existing Legacy BIOS / MBR / exFAT path; the
current bounded userland baseline adds files under `/boot/user` and `/bin` but
does not introduce UEFI, AHCI, NVMe, virtio, or a new filesystem backend. Every
user program remains a static freestanding ELF64 `ET_EXEC` image bounded by the
shared 64 KiB user-ELF limit.

## Validation

Default boot reaches PID-1 init and then `/bin/sh`; the QEMU headless default
marker is `BIGOS_USER_EXEC`. The default-off `userland_smoke` path is selected
with:

```bash
xmake f --userland_smoke=y
uv run python -m tools.bigosdev run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` validates the non-interactive runtime path. The simple C
program baseline adds behavior assertions for the smoke-only C probes: the smoke
observes their stdout/stderr, verifies argument and environment reporting,
verifies `errno` translation through failing wrappers and success paths that do
not rewrite `errno`, observes the requested exit-code probe, checks cwd-relative
open/stat/`..`, fork inheritance, exec preservation through `/bin/pwd`, shell
`cd`, and the bounded libc subset probe for public headers, ctype, time,
assert, unsigned conversion, stateless search helpers, formatter behavior,
error text, directory wrappers, and failure paths. It also runs probes through
`/bin/sh` to confirm the shell continues after a non-zero external program.
Interactive
console usability also
keeps the default-init headless marker assertion (`BIGOS_USER_EXEC`) while
adding optional manual or emulator-input checks for prompt visibility, input
echo, backspace feedback, and command output on the text console. When local
display, ROM, keyboard input, or injection support is unavailable, record the
interactive portion as skipped or blocked with the substitute
source/build/headless checks and remaining console-usability risk.
