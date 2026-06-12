# Runtime Smoke Validation

BigOS stage 9 productizes the existing default-off runtime smokes as a narrow validation matrix. The matrix is a tooling and documentation layer only: it does not add kernel runtime features, CI integration, UEFI support, storage drivers, or new smoke marker ABI.

## Matrix Runner

- Preferred automated command: `uv run python tools/boot_debug.py runtime-smoke-matrix`
- Single case command: `uv run python tools/boot_debug.py runtime-smoke-matrix --case memory-self-test`
- Artifact override: `uv run python tools/boot_debug.py runtime-smoke-matrix --output build/test/runtime-smoke-validation.md`
- Serial logs: one file per case under `build/test/runtime-smoke/` by default.
- Raw images: one Legacy BIOS/MBR/exFAT raw image per case under `build/test/runtime-smoke/` by default.

The runner explicitly configures each case through `xmake f`, builds through the existing xmake-backed flow, prepares the existing Legacy BIOS/MBR/exFAT raw image, launches QEMU with `--display none`, and waits for the expected COM1 marker within the case-specific timeout.

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
| `pipe` | `--pipe_smoke=y` | `BIGOS_PIPE_PASSED` | 30s | Pipe/dup endpoint accounting, blocking wakeup, EOF, and `EPIPE`. |
| `userland-runtime` | `--userland_smoke=y` | `BIGOS_USERLAND_PASSED` | 40s | crt0/libc wrappers, errno, fork/exec/wait, pipe, redirection, and malloc. |
| `default-init` | _(none)_ | `BIGOS_USER_EXEC` | 40s | Default build with no smoke switch; normal boot packages PID-1 init, `/bin/sh`, and bounded `/bin/*`. |

Each case enables only the listed smoke switch and explicitly disables the other smoke switches before building. Outside the runner, all runtime smoke options remain default-off unless a developer explicitly configures them with `xmake f ...=y`.

The `default-init` case is the behavior-assertion case driven by no smoke switch:
it builds the default configuration (every smoke option set to `=n`) and asserts
that normal boot reaches resident PID-1 init and `/bin/sh`, using
`BIGOS_USER_EXEC` as the QEMU headless marker. Missing that marker is a failure
and is not reinterpreted as a pass.

The `blocking-primitives` case emits intermediate markers `BIGOS_BLOCKING_WAIT_BLOCKED`, `BIGOS_BLOCKING_WAKE_SENT`, `BIGOS_BLOCKING_WAIT_RESUMED`, `BIGOS_BLOCKING_TIMEOUT_BLOCKED`, and `BIGOS_BLOCKING_TIMEOUT_EXPIRED` before the final pass marker. It uses a synthetic TTY producer, so automated QEMU headless validation does not require manual keyboard input; optional manual keyboard validation should be recorded separately when performed.

The `scheduler-semantics` case emits intermediate markers `BIGOS_SCHED_SEMANTICS_START`, `BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED`, and `BIGOS_SCHED_SEMANTICS_PREEMPTED` before the final pass marker. It exercises time-slice expiry and timer-driven IRQ-return reschedule without enabling memory, filesystem, user-program, user-ELF, or broad smoke options. Because it touches IRQ/timer/context-switch behavior, validation notes should record QEMU headless serial logs and whether Bochs or QEMU+Bochs cross-validation was executed or skipped.

The process lifecycle core now compiles in normal builds. User-program smoke
cases validate smoke-only entry threads and marker behavior, while source-level
checks cover PID uniqueness, bounded table failure, parent/child linkage,
zombie-to-reap, wait wakeups, exec rollback, bounded `argv`/`envp`, active-root
teardown rejection, and current-stack release deferral.

The fd/VFS shell is validated by source-level checks plus `filesystem-read`,
`filesystem-user-elf`, `writable-fs`, `pipe`, and `userland-runtime` runtime
cases. The read-only exFAT path remains the boot/image source of truth, while
`/rw` and pipe semantics are bounded runtime capabilities. fd/VFS syscalls use
the DPL=3 `int 0x80` trap gate and must pass `sched::can_block()` before
synchronous storage I/O or blocking pipe operations.

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

## Cross-Validation

QEMU headless is the preferred automated serial-marker path for the matrix. Bochs or QEMU+Bochs cross-validation remains scenario-specific for changes that affect boot, real-mode/protected-mode/long-mode transition, interrupt dispatch, timer IRQ, keyboard IRQ, ATA PIO, port IO, or low-level driver behavior.

When Bochs cross-validation is unavailable, record why it was skipped, which QEMU, build, source-level, or manual checks were used instead, and what residual hardware-behavior risk remains.

## Preserved Contracts

Runtime smoke productization must not change kernel link addresses, BootInfo or handoff ABI, page-table assumptions, IDT vectors, IRQ EOI rules, syscall vector `0x80`, CR3 switching rules, smoke marker strings, or default-off smoke entry boundaries. The image path remains the existing Legacy BIOS raw image with MBR/exFAT, `/boot/boot.bin`, root `kernel`, and IDE-compatible disk exposure; it does not require UEFI, OVMF, ESP/FAT, virtio, AHCI/SATA, NVMe, or a new storage driver.
