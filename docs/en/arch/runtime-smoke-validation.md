# Runtime Smoke Validation

BigOS productizes the existing default-off runtime smokes as a narrow validation matrix for the current bounded baseline. The matrix is a tooling and documentation layer only: it does not add kernel runtime features, CI integration, UEFI runtime parity, storage drivers, or new smoke marker ABI.

## Matrix Runner

- Preferred automated command: `uv run python tools/boot_debug.py runtime-smoke-matrix`
- Single case command: `uv run python tools/boot_debug.py runtime-smoke-matrix --case memory-self-test`
- Artifact override: `uv run python tools/boot_debug.py runtime-smoke-matrix --output build/test/runtime-smoke-validation.md`
- Serial logs: one file per case under `build/test/runtime-smoke/` by default.
- Raw images: one Legacy BIOS/MBR/exFAT raw image per case under `build/test/runtime-smoke/` by default.

The runner explicitly configures each case through `xmake f`, builds through the existing xmake-backed flow, prepares the existing Legacy BIOS/MBR/exFAT raw image, launches QEMU with `--display none`, and waits for the expected COM1 marker within the case-specific timeout.

## Validation Inventory

Behavior-oriented validation distinguishes three entry classes:

- Default path: the `default-init` case uses the normal build with all smoke switches disabled, packages PID-1 init, `/bin/sh`, and bounded `/bin/*`, then observes the deterministic init/shell serial markers through QEMU headless logs.
- Default-off smoke: `userland-runtime`, `filesystem-maturity`, `writable-fs`, `pipe`, `filesystem-read`, `filesystem-user-elf`, and related cases enable one explicit smoke switch at a time to validate userland, process/fd, pipe/redirection, and filesystem behavior without changing normal boot defaults.
- Scenario evidence: graphical QEMU, Bochs, manual keyboard input, emulator input injection, or hardware-adjacent checks are recorded only when they add evidence for console usability or low-level boot/IRQ/timer/ATA/port-IO risks.

## Matrix Cases

| Case | xmake switches | Expected marker | Timeout | Boundary |
| --- | --- | --- | ---: | --- |
| `memory-self-test` | `--mm_self_test=y` | `BIGOS_MM_SELF_TEST_PASSED` | 10s | Early allocator and direct-map self-test. |
| `timer-irq` | `--timer_smoke=y` | `BIGOS_TIMER_IRQ` | 10s | PIC/PIT IRQ0 marker path. |
| `scheduler` | `--scheduler_smoke=y` | `BIGOS_SCHED_THREAD_B` | 10s | Cooperative kernel-thread context switch path. |
| `scheduler-semantics` | `--scheduler_semantics_smoke=y` | `BIGOS_SCHED_SEMANTICS_PASSED` | 15s | Timer slice expiry, preemption-disable deferral, and guarded IRQ-return reschedule. |
| `blocking-primitives` | `--blocking_smoke=y` | `BIGOS_BLOCKING_SMOKE_PASSED` | 15s | Synthetic TTY producer plus wait queue wakeup and timeout sleep. |
| `syscall` | `--syscall_smoke=y` | `BIGOS_SYSCALL_SMOKE_PASSED` | 10s | `int 0x80` minimal syscall ABI path. |
| `filesystem-read` | `--fs_smoke=y` | `BIGOS_FS_EXFAT_READ_PASSED` | 20s | ATA PIO plus VFS open/read/release over the read-only exFAT backend. |
| `first-user-program` | `--user_program_smoke=y` | `BIGOS_USER_EXIT` | 20s | Runs the embedded flat image as a lifecycle-core process; smoke entry remains default-off. |
| `filesystem-user-elf` | `--user_elf_smoke=y` | `BIGOS_USER_EXIT` | 30s | Packages `/boot/user/init.elf` and runs it through reusable ELF exec preparation; smoke entry remains default-off. |
| `demand-paging` | `--demand_paging_smoke=y` | `BIGOS_DEMAND_PAGING_PASSED` | 30s | VMA-backed lazy anonymous materialization and deterministic fault handling. |
| `fork-cow` | `--fork_cow_smoke=y` | `BIGOS_FORK_COW_PASSED` | 30s | Bounded `fork` plus anonymous COW split semantics. |
| `time-identity` | `--time_identity_smoke=y` | `BIGOS_TIME_IDENTITY_PASSED` | 20s | Wall-clock and pid/ppid/uid/gid syscall path. |
| `signals` | `--signal_smoke=y` | `BIGOS_SIGNAL_PASSED` | 30s | Minimal signal queue, masks, handlers, and delivery path. |
| `writable-fs` | `--writable_fs_smoke=y` | `BIGOS_WRITABLE_FS_PASSED` | 30s | RAM-backed `/rw`, page/buffer cache, write/readback, fsync, and permissions. |
| `persistent-writable-fs-write` | `--persistent_writable_fs_smoke=y` | `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED` | 40s | First boot with `--persistent-image`: explicit format of the independent test disk, bounded write, `fsync`, and cache-eviction readback. |
| `persistent-writable-fs-verify` | `--persistent_writable_fs_smoke=y` | `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED` | 40s | Second boot with the same `--persistent-image`: mount-existing and read synchronized `/rw` state after clean reboot. |
| `pipe` | `--pipe_smoke=y` | `BIGOS_PIPE_PASSED` | 30s | Pipe/dup endpoint accounting, blocking wakeup, EOF, and `EPIPE`. |
| `filesystem-maturity` | `--filesystem_maturity_smoke=y` | `BIGOS_FILESYSTEM_MATURITY_PASSED` | 40s | runtime filesystem maturity current-runtime filesystem semantics across read-only exFAT, RAM-backed `/rw`, fd/VFS, metadata, cwd-relative paths, libc errno, and shell-visible tools; no reboot persistence. |
| `userland-runtime` | `--userland_smoke=y` | `BIGOS_USERLAND_PASSED` | 40s | crt0/libc wrappers, arg/env handoff, stdout/stderr, errno, `snprintf`/formatter, `strtol`/`atoi`, `calloc`/`realloc`, bounded `DIR*` wrappers, simple C program baseline probes, shell execution, fork/exec/wait, pipe, redirection, and bounded `/rw` runtime file operations. |
| `default-init` | _(none)_ | `BIGOS_USER_EXEC` | 40s | Default build with no smoke switch; normal boot packages PID-1 init, `/bin/sh`, and bounded `/bin/*`. |

Each case enables only the listed smoke switch and explicitly disables the other smoke switches before building. Outside the runner, all runtime smoke options remain default-off unless a developer explicitly configures them with `xmake f ...=y`.

## Behavior-Oriented Matrix

The current bounded minimal usable system baseline promotes the runtime matrix from marker-only smoke coverage to behavior assertions for the bounded minimal usable system. Each row records the exercised capability, deterministic input, expected observable result, failure signal, validation layer, and environment dependency. These checks remain bounded to the default x86_64 Legacy BIOS/MBR/exFAT backend and do not require UEFI runtime parity, OVMF parity, ESP/FAT runtime storage parity, virtio, AHCI/SATA, NVMe, SMP, dynamic linking, job control, full shell grammar, or a complete POSIX libc.

| Capability | Input or path | Expected observable result | Failure signal | Layer | Environment dependency |
| --- | --- | --- | --- | --- | --- |
| Default init and `/bin/sh` reachability | `default-init` normal boot, no smoke switch | PID-1 init starts and resident `/bin/sh` is launched, observed by `BIGOS_INIT_ENTER` then `BIGOS_USER_EXEC` | Missing expected marker, timeout, emulator exit, or panic marker | QEMU headless runtime assertion | xmake, cross-binutils, QEMU, serial log, Legacy BIOS raw image |
| Simple C args/env/stdout/stderr | `userland-runtime` runs `/bin/smoke/args`, `/bin/smoke/env`, and `/bin/smoke/out` | `/rw` records expected `argc`/`argv`, deterministic environment boundary text, and stdout/stderr transcript content | `BIGOS_USERLAND_FAILED <reason>`, missing `/rw` record, wrong exit status, or missing pass marker | Default-off userland runtime assertion | xmake, cross-binutils, QEMU, serial log, RAM-backed `/rw` |
| Simple C `errno` and exit status | `userland-runtime` runs failing open/exec wrappers and `/bin/smoke/exit 7` | Error wrappers report documented `errno`, failed `execve` leaves caller alive, parent observes requested child status | Failure marker, mismatched status, wrong `errno`, or missing continuation output | Default-off userland runtime assertion | xmake, cross-binutils, QEMU, serial log |
| Shell continuation and unsupported syntax | Non-interactive `/bin/sh` script runs a non-zero program, unsupported pipe syntax, then `echo shell-alive` | Shell reports deterministic syntax/error text and continues to the next command | Missing error text, missing `shell-alive`, shell crash, or missing pass marker | Default-off userland runtime assertion; manual interactive evidence optional | QEMU headless for scripted assertion; display/input only for optional interactive notes |
| `exec`/`wait` and fd inheritance | Userland smoke forks, execs packaged programs, waits, and preserves stdio or redirected descriptors | Child output or `/rw` records are visible, parent wait returns the expected child/status, inherited descriptors remain usable | Failed wait, wrong status, missing output, descriptor failure, or failure marker | Default-off userland runtime assertion | xmake, cross-binutils, QEMU, serial log |
| `dup`, redirection, and unrelated fd state | Userland smoke duplicates an fd, closes the original, redirects shell output to `/rw`, and reads it back | Duplicate fd writes survive original close; redirected file contains expected data; shell transcript remains usable | Wrong file content, failed dup/readback, shell fd corruption, or failure marker | Default-off userland runtime assertion | QEMU headless, serial log, RAM-backed `/rw` |
| Pipe endpoint behavior | Userland smoke transfers `pipe-data`; shell runs `echo pipe-ok | /bin/cat`; `pipe` smoke checks endpoint accounting | Downstream reader sees the bytes, EOF appears after writers close, unrelated fds remain usable | Wrong data, missing EOF, `EPIPE`/endpoint mismatch, missing pass marker, or panic | Default-off userland runtime assertion plus narrow pipe smoke | QEMU headless, serial log |
| Runtime `/rw` filesystem operations | Userland/filesystem maturity smoke creates files/dirs, writes, fsyncs, seeks, reads back, enumerates `/rw`, renames, unlinks, and checks read-only backend rejection | File contents, metadata, directory entries, stable backend order, open-fd lifetime, and `EROFS`/`ENOENT`/`EEXIST`/`ENOSPC`/`ERANGE` errors match the bounded VFS contract | Wrong content, missing dirent, unexpected persistence requirement, wrong `errno`, or failure marker | Default-off filesystem maturity/userland assertion plus writable-fs smoke | QEMU headless, serial log, RAM-backed `/rw` |
| Low-level boot/IRQ/timer/storage behavior | Narrow memory/timer/scheduler/blocking/filesystem cases; Bochs or QEMU+Bochs when relevant | Expected markers and optional intermediate markers appear; cross-emulator notes record result when available | Missing marker, panic, timeout, skipped cross-validation without risk note | Source/spec, build, QEMU headless, optional Bochs/graphical evidence | Toolchain plus selected emulator; Bochs/display/ROM only when scenario requires |

The `default-init` case is the behavior-assertion case driven by no smoke switch:
it builds the default configuration (every smoke option set to `=n`) and asserts
that normal boot reaches resident PID-1 init and `/bin/sh`, using
`BIGOS_USER_EXEC` as the QEMU headless marker. Missing that marker is a failure
and is not reinterpreted as a pass.

Interactive console usability validation layers on top of this case. Automated
QEMU headless runs continue to use serial/log marker assertions and do not
require graphical display, manual keyboard input, or emulator scancode
injection. When graphical QEMU, Bochs, manual keyboard input, or input injection
is available, validation notes should record the backend, display/input method,
typed command, observed prompt/echo/output, EOF-like input, interrupt-like line
cancellation, unsupported-control behavior, and result. If those capabilities
are unavailable, mark the interactive portion skipped or blocked and record the
source-level, build, and headless checks used as substitutes plus the remaining
console-usability risk.

The `blocking-primitives` case emits intermediate markers `BIGOS_BLOCKING_WAIT_BLOCKED`, `BIGOS_BLOCKING_WAKE_SENT`, `BIGOS_BLOCKING_WAIT_RESUMED`, `BIGOS_BLOCKING_TIMEOUT_BLOCKED`, and `BIGOS_BLOCKING_TIMEOUT_EXPIRED` before the final pass marker. It uses a synthetic TTY producer, so automated QEMU headless validation does not require manual keyboard input; optional manual keyboard validation should be recorded separately when performed.

The `scheduler-semantics` case emits intermediate markers `BIGOS_SCHED_SEMANTICS_START`, `BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED`, and `BIGOS_SCHED_SEMANTICS_PREEMPTED` before the final pass marker. It exercises time-slice expiry and timer-driven IRQ-return reschedule without enabling memory, filesystem, user-program, user-ELF, or broad smoke options. Because it touches IRQ/timer/context-switch behavior, validation notes should record QEMU headless serial logs and whether Bochs or QEMU+Bochs cross-validation was executed or skipped.

The process lifecycle core now compiles in normal builds. User-program smoke
cases validate smoke-only entry threads and marker behavior, while source-level
checks cover PID uniqueness, bounded table failure, parent/child linkage,
zombie-to-reap, wait wakeups, exec rollback, bounded `argv`/`envp`, active-root
teardown rejection, and current-stack release deferral.

The fd/VFS shell is validated by source-level checks plus `filesystem-read`,
`filesystem-user-elf`, `writable-fs`, `pipe`, `filesystem-maturity`, and `userland-runtime` runtime
cases. The read-only exFAT path remains the boot/image source of truth, while
`/rw` and pipe semantics are bounded runtime capabilities. RAM-backed `/rw`
guarantees current-runtime consistency only and does not persist data across
reboot or alter the Legacy BIOS/MBR/exFAT disk image. Persistent `/rw` validation
uses an independent test disk and only claims clean-sync plus clean-reboot
visibility; missing QEMU/Bochs support for the extra disk must be recorded as
skipped or blocked with residual storage-risk notes. fd/VFS syscalls use the
DPL=3 `int 0x80` trap gate and must pass `sched::can_block()` before synchronous
storage I/O or blocking pipe operations.

Simple C program validation is layered into the default-off
`userland-runtime` case. When `userland_smoke` is enabled, the build packages
bounded `/bin/smoke/args`, `/bin/smoke/env`, `/bin/smoke/out`,
`/bin/smoke/errno`, and `/bin/smoke/exit` programs through the same
`crt0 + libc + -nostdlib -static` ELF64 path as `/bin/sh`, `/bin/echo`, and
`/bin/cat`. These probes are not packaged in the normal image. The smoke
observes program stdout/stderr, checks argument and environment reporting,
checks a failing wrapper's `errno` translation, observes the requested exit-code
probe, and feeds a deterministic command script into `/bin/sh` to confirm the
shell continues after an external program returns a non-zero status. This
validation does not add a new kernel syscall, change the `int 0x80` ABI, change
boot/disk layout, or make emulator-dependent smoke mandatory for default builds.

## Manual Single-Case Flow

Manual validation remains useful when debugging a single failure. Record the command, smoke switch, expected marker, serial log, result, skipped matrix cases, substitute checks, and residual risk in review notes or in the generated artifact.

Example:

```bash
xmake f --mm_self_test=y
uv run python tools/boot_debug.py run \
  --emulator qemu \
  --display none \
  --serial-log build/test/runtime-smoke/memory-self-test.serial.log \
  --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED \
  --smoke-timeout 10
```

## Artifact Fields

The runner writes a Markdown-first validation artifact to `build/test/runtime-smoke-validation.md` unless `--output` is provided. The artifact keeps JSON schema compatible fields for future automation:

- `schema_version`: runtime smoke validation schema version.
- `tool availability`: `uv`, `xmake`, `x86_64-elf-*`, QEMU, and optionally Bochs.
- `case id`: stable matrix case identifier.
- `xmake configuration`: explicit smoke switches used for the case.
- `expected marker` and `observed marker`: COM1 marker comparison.
- `blocking markers`: for the blocking case, intermediate wait/wake/timeout markers when present in the serial log.
- `scheduler semantics markers`: for the scheduler semantics case, delayed-preemption and IRQ-return-preempted markers when present in the serial log.
- `serial log path`: generated log used as the source of truth.
- `timeout` and `exit status`: bounded wait and failure context.
- `status`: `passed`, `failed`, `skipped`, or `blocked`.
- `failed stage`: preflight, build, image build, validation, or emulator marker stage.
- `skip reason`, `alternative checks`, and `residual risk`: required for unavailable tools or skipped cross-validation.

Missing `uv`, `xmake`, cross-binutils, QEMU, Bochs, ROM/display configuration, or other required local dependencies must be recorded as skipped or blocked. A smoke that did not run must not be marked as passed.

## UEFI Spike Smoke

The x86_64 UEFI boot backend spike has a separate smoke entry and does not change the Legacy runtime matrix. Use `xmake run qemu-uefi -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40` or the direct helper form `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --serial-log build/test/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`.

The UEFI smoke builds/uses `BOOTX64.EFI`, creates an ESP/FAT image with the kernel, PID-1 init, `/bin/sh`, and bounded `/bin/*`, launches QEMU with x86_64 OVMF, and uses `build/test/qemu-uefi.serial.log` by default. It requires QEMU/OVMF, Homebrew LLVM/LLD, `mtools`, the existing x86_64 cross toolchain, and `uv` for Python helper validation. Missing OVMF, mtools, LLVM/LLD, QEMU, the cross toolchain, or `uv` must be recorded as skipped or blocked with substitute checks and residual UEFI bootability risk.

The expected default UEFI runtime marker is the same default init/user exec marker as the Legacy BIOS default headless path, currently `BIGOS_USER_EXEC`. Missing that marker is a failed or blocked UEFI runtime-parity check, not a pass. Apple Silicon hosts may run x86_64 QEMU through TCG, so validation notes should record timeout values and performance-related residual risk when applicable.

## Cross-Validation

QEMU headless is the preferred automated serial-marker path for the matrix. Bochs or QEMU+Bochs cross-validation remains scenario-specific for changes that affect boot, real-mode/protected-mode/long-mode transition, interrupt dispatch, timer IRQ, keyboard IRQ, ATA PIO, port IO, or low-level driver behavior.

When Bochs cross-validation is unavailable, record why it was skipped, which QEMU, build, source-level, or manual checks were used instead, and what residual hardware-behavior risk remains.

## Preserved Contracts

Runtime smoke productization must not change kernel link addresses, BootInfo or handoff ABI, page-table assumptions, IDT vectors, IRQ EOI rules, syscall vector `0x80`, CR3 switching rules, smoke marker strings, or default-off smoke entry boundaries. The Legacy matrix image path remains the existing Legacy BIOS raw image with MBR/exFAT, `/boot/boot.bin`, root `kernel`, and IDE-compatible disk exposure; it does not require UEFI, OVMF, ESP/FAT, virtio, AHCI/SATA, NVMe, or a new storage driver. The UEFI spike uses its own explicit ESP/QEMU/OVMF path and must not be counted as replacing the Legacy matrix.
