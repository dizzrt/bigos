# Userland Runtime, libc, and Shell

BigOS now has a bounded, freestanding userland path built from C sources under
`user/`. It is still a minimal research-kernel userland, not a complete POSIX
environment.

## Components

- `user/crt0/crt0.s`: user entry `_start`. It consumes the initial stack produced
  by `copy_exec_args_to_stack` in `kernel/core/proc/proc.cc`.
- `user/libc`: bounded minimal C library subset, syscall wrappers, errno
  translation, string/memory helpers, `brk`-backed `malloc`/`free`, tiny
  fd-backed stdio with opaque standard streams, `printf`,
  `fprintf(stderr, ...)`, and read-only `environ`/`getenv`.
- `user/init/init.c`: resident PID-1. It starts `/bin/sh` with
  `fork` + `execve`, waits for children, and restarts the shell when it exits.
- `user/sh/sh.c`: bounded interactive shell with builtins, PATH lookup,
  `fork` + `execve` + `wait`, one-stage pipes, and basic `<` / `>` redirection.
- `user/bin/*`: small packaged user binaries such as `/bin/echo` and `/bin/cat`.
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
`-EACCES`, `-ENOEXEC`, `-E2BIG`, `-EFAULT`, or `-ENOMEM`. The kernel copies
`path`, `argv`, and `envp` through VMA-backed user-buffer validation and bounded
`EXEC_MAX_*` limits before reading the target ELF through VFS.

The user libc mirror headers `user/libc/include/sys_nr.h` and
`user/libc/include/errno.h` intentionally do not include C++ kernel headers.
`tests/test_syscall_entry_source.py` asserts that their values match
`include/bigos/syscall.h` and `include/bigos/errno.h`.

## Shell Bounds

The shell is intentionally small:

- Builtins: `exit` and `echo`.
- Prompt: deterministic `$ ` only when stdin and stdout are still connected to
  the default console fast paths.
- Input feedback: printable characters, newline, and backspace are echoed from
  the non-interrupt shell consumer after `read(0, ...)` returns.
- Command lookup: paths containing `/` execute directly; other names are tried
  against `PATH`, with `/bin` as the default.
- Execution: external commands run via `fork` + `execve` + `wait`.
- Pipes: one `a | b` stage.
- Redirection: one input `< file` and one output `> file` per command.
- Output: builtins, child stdout, and deterministic recoverable errors use the
  existing fd/syscall path; default fd `1` and fd `2` reach the visible console
  when not redirected.
- Capacity: line length, argument count, PATH candidates, and path lengths are
  fixed upper bounds in `user/sh/sh.c`.

This does not implement job control, background processes, globbing, variable
expansion, shell scripts, sub-shells, terminal process groups, termios, a full
FILE API, dynamic linking, or a complete POSIX libc.

## Minimal libc Subset

The user libc exposes a documented bounded subset for simple static C programs:

- Headers: `stdio.h`, `stdlib.h`, `string.h`, `errno.h`, `unistd.h`,
  `fcntl.h`, `sys/types.h`, `sys/wait.h`, plus the compatibility umbrella
  `libc.h`.
- Types and constants: `size_t`, `ssize_t`, `off_t`, `pid_t`, `NULL`, the
  implemented open flags, seek constants, `WAIT_ANY`, and errno values mirrored
  from `include/bigos/errno.h`.
- Syscall wrappers: negative kernel errno returns become positive user `errno`
  and `-1` or the documented failure sentinel; successful wrappers do not clear
  or rewrite an existing `errno` value.
- Strings and memory: the subset includes the implemented bounded routines such
  as `strlen`, `strcmp`, `strncmp`, `memcpy`, `memset`, and overlap-safe
  `memmove`. Null pointer inputs keep ordinary C preconditions and are not given
  extra BigOS-specific safety promises.
- Heap: `malloc` returns 16-byte-aligned writable memory or `NULL` on bounded
  failure without corrupting existing blocks. `free(NULL)` is a no-op. The
  allocator does not promise thread safety, complete coalescing, `realloc`, or
  hosted allocator behavior.
- Stdio: `stdin`, `stdout`, and `stderr` are opaque handles for fd `0`, `1`, and
  `2` only. `putchar`, `puts`, `printf`, and `fprintf(stderr, ...)` are fd/write
  based and support `%s`, `%d`, `%x`, `%c`, and `%%`; there is no `fopen`,
  `fclose`, full buffering, locale, floating-point formatting, wide-character
  support, or hosted `FILE` semantics.
- Environment: `envp`, `environ`, and `getenv` are read-only. This stage does
  not implement `setenv`, `putenv`, or `unsetenv`.

## Simple C Program Baseline

The simple C program baseline treats simple static C programs as a user-visible compatibility
baseline, still within the existing freestanding runtime boundary:

- Entry: `_start` calls `main(argc, argv, envp)` using the existing user stack
  layout, and `main`'s return value is passed to `SYS_EXIT`.
- Wrappers: libc syscall wrappers translate negative kernel returns into
  positive `errno` values and return `-1` or the documented failure sentinel.
- Output: programs use fd-based `write`, `putchar`, `puts`, or the tiny `printf`;
  stdout is fd `1` and deterministic errors can be written to fd `2`.
- Environment: `envp`, `environ`, and `getenv` are read-only. If no environment
  is supplied, programs must report that empty boundary deterministically.
- Smoke-only probes: `/bin/smoke/args`, `/bin/smoke/env`, `/bin/smoke/out`,
  `/bin/smoke/errno`, `/bin/smoke/exit`, and `/bin/smoke/libc_subset` cover
  argument handoff, environment reporting, stdout/stderr, wrapper failure plus
  `errno`, requested exit status, fine-grained libc headers,
  `fprintf(stderr, ...)`, string/memory boundaries, and bounded heap behavior
  when `userland_smoke` is enabled.

This baseline does not add kernel syscalls, change the `int 0x80` register ABI,
change boot or disk layout, introduce dynamic linking, or claim hosted libc or
full POSIX shell behavior.

## Build and Packaging

`xmake.lua` builds user C programs with the cross toolchain as static
freestanding ELF64 `ET_EXEC` images using `user/crt0`, `user/libc`, and
`user/link.lds`. The boot image packages:

- `/boot/user/init.elf` for the default resident C init or the selected smoke.
- `/bin/sh` for the interactive shell.
- `/bin/echo` and `/bin/cat` for normal packaged user commands.
- `/bin/smoke/*` probes only for the explicit `userland_smoke` validation image.

The image layout remains the existing Legacy BIOS / MBR / exFAT path; stage 19
and later userland stages add files under `/boot/user` and `/bin` but do not
introduce UEFI, AHCI, NVMe, virtio, or a new filesystem backend. Every user
program remains a static freestanding ELF64 `ET_EXEC` image bounded by the shared
64 KiB user-ELF limit.

## Validation

Default boot reaches PID-1 init and then `/bin/sh`; the QEMU headless default
marker is `BIGOS_USER_EXEC`. The default-off `userland_smoke` path is selected
with:

```bash
xmake f --userland_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` validates the non-interactive runtime path. The simple C
program baseline adds behavior assertions for the smoke-only C probes: the smoke
observes their stdout/stderr, verifies argument and environment reporting,
verifies `errno` translation through failing wrappers and success paths that do
not rewrite `errno`, observes the requested exit-code probe, checks the bounded
libc subset probe, and runs probes through `/bin/sh` to confirm the shell
continues after a non-zero external program. Interactive console usability also
keeps the default-init headless marker assertion (`BIGOS_USER_EXEC`) while
adding optional manual or emulator-input checks for prompt visibility, input
echo, backspace feedback, and command output on the text console. When local
display, ROM, keyboard input, or injection support is unavailable, record the
interactive portion as skipped or blocked with the substitute
source/build/headless checks and remaining console-usability risk.
