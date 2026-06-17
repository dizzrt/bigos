# Validation Record

## Scope And Boundary Review

- Reviewed `roadmap.md` process/terminal/shell composition hardening and this change's proposal, design, specs, and tasks.
- Confirmed the implementation scope remains the bounded process, default terminal, and shell composition subset.
- Confirmed this change does not require sessions, terminal process groups, job control, termios, a complete POSIX shell, a complete POSIX libc, dynamic linking, SMP, async I/O, persistent full writable filesystems, or broad file-backed `mmap`.
- Confirmed no source changes alter boot handoff, linker address, page-table layout, direct map, GDT/TSS, CR3 switching, `InterruptFrame`, syscall vector `0x80`, disk layout, or user ELF ABI.

## Source Review

- `kernel/core/proc/proc.cc` keeps `wait_current()` bounded to exact child PID or `WAIT_ANY`, returns `-ECHILD` for no matching direct child, writes status only after a waitable match, and hands the matched child to the existing reap path.
- `user/libc/syscall.c` rejects unsupported `waitpid()` options with `errno = EINVAL` before invoking `SYS_WAIT`, so the path does not block, reap, or modify status storage for unsupported options.
- `kernel/core/signal/signal.cc` maps default terminate and `SIGKILL` to the existing fault-to-reaper lifecycle with status `-(128 + signo)`.
- `user/libc/include/sys/wait.h` documents the bounded BigOS wait status helpers and avoids complete POSIX stopped, continued, process-group, or job-control claims.
- `kernel/core/terminal/keyboard.cc` keeps keyboard IRQ1 handling limited to fixed decoder state updates and bounded TTY enqueue; no allocation, blocking wait, filesystem, ordinary echo, or shell policy is performed in IRQ context.
- `kernel/core/terminal/tty.cc` classifies newline/carriage return, backspace, delete-like, EOF-like, interrupt-like, and unsupported control bytes; EOF-like becomes a deterministic empty-read result and interrupt-like is passed as `0x03` for user shell line cancellation.
- `user/sh/sh.c` consumes terminal EOF-like and interrupt-like results in the read loop, maintains bounded `last_status`, reports deterministic syntax and exec failures, and returns to the prompt when the shell itself is not exiting.
- `user/sh/sh.c` moves temporary pipe/redirection fds away from stdio, closes temporary fds on setup failures, and preserves parent shell stdin/stdout/stderr across successful and failed command setup.
- `user/smoke/userland_smoke.c` already covers wait wrapper behavior, shell external command status, command-not-found status, unsupported syntax status, pipe output, redirection output, cwd/PATH composition, and deterministic path-tool failures.

## Passed Checks

- `openspec validate improve-process-terminal-shell-compatibility --strict`
  - Passed: change is valid.
- `xmake`
  - Passed: `build ok`, 1.241s.
- `clang++ --target=x86_64-elf -fsyntax-only -std=c++17 -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_USER_PROCESS -DBIGOS_WRITABLE_FS_SMOKE -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx kernel/core/terminal/keyboard.cc kernel/core/terminal/tty.cc kernel/core/syscall/syscall.cc kernel/core/signal/signal.cc kernel/core/proc/proc.cc`
  - Passed with no diagnostics.
- `clang --target=x86_64-elf -fsyntax-only -std=c17 -Iuser/libc/include -Iinclude -ffreestanding -fno-builtin -mno-red-zone user/sh/sh.c user/libc/syscall.c user/smoke/userland_smoke.c`
  - Passed with no diagnostics.
- `xmake f --userland_smoke=y && xmake && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-composition-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`
  - Passed: QEMU observed serial marker `BIGOS_USERLAND_PASSED`.
  - Covered shell-launched child normal exit, deterministic command failure, wait-status recovery, pipe/redirection fd composition, cwd/PATH utility composition, and shell recovery after failures through the existing userland smoke.
- `xmake f --userland_smoke=n`
  - Ran after smoke validation to restore the default-off validation switch.

## Tooling Availability

- `xmake`: available at `/opt/homebrew/bin/xmake`, version `v3.0.9+20260519`.
- `x86_64-elf-gcc`: available at `/usr/local/bin/cross_compiler/bin/x86_64-elf-gcc`.
- `x86_64-elf-g++`: available at `/usr/local/bin/cross_compiler/bin/x86_64-elf-g++`.
- `qemu-system-x86_64`: available at `/opt/homebrew/bin/qemu-system-x86_64`.
- `bochs`: available at `/opt/homebrew/bin/bochs`.
- `uv`: available at `/opt/homebrew/bin/uv`.
- `clang++`: available at `/opt/homebrew/opt/llvm/bin/clang++`.

## Skipped Checks

- Manual QEMU/Bochs console-input observation for live newline, backspace/delete-like, EOF-like, and interrupt-like key sequences was skipped because this session used the headless automated QEMU path and has no interactive console-input oracle.
- Bochs cross-validation was skipped because QEMU headless userland smoke covered the deterministic process/shell/fd composition paths, while the remaining keyboard-control observation requires manual interactive input.
- Python helper validation with `uv run ruff check`, `uv run ruff format --check`, `uv run pyright`, and `uv run pytest` was not applicable because this change did not modify Python helper or validation script files.

## Remaining Risk

- EOF-like and interrupt-like behavior is source-reviewed and documented through the TTY and shell paths, but live keyboard observation remains a manual validation gap for this session.
- The automated runtime smoke exercises non-interactive shell input through pipes rather than physical PS/2 keyboard input, so keyboard-device-specific console interaction risk remains bounded but not fully observed here.
