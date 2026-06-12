# Userland Runtime, libc, and Shell

BigOS now has a bounded, freestanding userland path built from C sources under
`user/`. It is still a minimal research-kernel userland, not a complete POSIX
environment.

## Components

- `user/crt0/crt0.s`: user entry `_start`. It consumes the initial stack produced
  by `copy_exec_args_to_stack` in `kernel/core/proc/proc.cc`.
- `user/libc`: minimal C runtime support, syscall wrappers, errno translation,
  string/memory helpers, `brk`-backed `malloc`/`free`, tiny stdio/printf, and
  read-only `environ`/`getenv`.
- `user/init/init.c`: resident PID-1. It starts `/bin/sh` with
  `fork` + `execve`, waits for children, and restarts the shell when it exits.
- `user/sh/sh.c`: bounded interactive shell with builtins, PATH lookup,
  `fork` + `execve` + `wait`, one-stage pipes, and basic `<` / `>` redirection.
- `user/bin/*`: small packaged user binaries such as `/bin/echo`.
- `user/smoke/userland_smoke.c`: default-off deterministic validation program
  for crt0, libc wrappers, errno, fork/exec/wait, pipe, redirection, and malloc.

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

## Build and Packaging

`xmake.lua` builds user C programs with the cross toolchain as static
freestanding ELF64 `ET_EXEC` images using `user/crt0`, `user/libc`, and
`user/link.lds`. The boot image packages:

- `/boot/user/init.elf` for the default resident C init or the selected smoke.
- `/bin/sh` for the interactive shell.
- `/bin/echo` and other bounded test binaries.

The image layout remains the existing Legacy BIOS / MBR / exFAT path; stage 19
adds files under `/boot/user` and `/bin` but does not introduce UEFI, AHCI, NVMe,
virtio, or a new filesystem backend.

## Validation

Default boot reaches PID-1 init and then `/bin/sh`; the QEMU headless default
marker is `BIGOS_USER_EXEC`. The default-off `userland_smoke` path is selected
with:

```bash
xmake f --userland_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` validates the non-interactive runtime path. Stage 20 also
keeps the default-init headless marker assertion (`BIGOS_USER_EXEC`) while adding
optional manual or emulator-input checks for prompt visibility, input echo,
backspace feedback, and command output on the text console. When local display,
ROM, keyboard input, or injection support is unavailable, record the interactive
portion as skipped or blocked with the substitute source/build/headless checks
and remaining console-usability risk.
