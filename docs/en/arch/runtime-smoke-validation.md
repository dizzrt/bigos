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
| `syscall` | `--syscall_smoke=y` | `BIGOS_SYSCALL_SMOKE_PASSED` | 10s | `int 0x80` minimal syscall ABI path. |
| `filesystem-read` | `--fs_smoke=y` | `BIGOS_FS_EXFAT_READ_PASSED` | 20s | ATA PIO plus read-only exFAT file read path. |
| `first-user-program` | `--user_program_smoke=y` | `BIGOS_USER_EXIT` | 20s | Compiles `src/kernel/proc/**`; not part of normal boot. |
| `filesystem-user-elf` | `--user_elf_smoke=y` | `BIGOS_USER_EXIT` | 30s | Compiles `src/kernel/proc/**` and packages `/boot/user/init.elf`; not part of normal boot. |

Each case enables only the listed smoke switch and explicitly disables the other smoke switches before building. Outside the runner, all runtime smoke options remain default-off unless a developer explicitly configures them with `xmake f ...=y`.

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
- `serial log path`: generated log used as the source of truth.
- `timeout` and `exit status`: bounded wait and failure context.
- `status`: `passed`, `failed`, `skipped`, or `blocked`.
- `failed stage`: preflight, build, image build, validation, or emulator marker stage.
- `skip reason`, `alternative checks`, and `residual risk`: required for unavailable tools or skipped cross-validation.

Missing `uv`, `xmake`, cross-binutils, QEMU, Bochs, ROM/display configuration, or other required local dependencies must be recorded as skipped or blocked. A smoke that did not run must not be marked as passed.

## Cross-Validation

QEMU headless is the preferred automated serial-marker path for the matrix. Bochs or QEMU+Bochs cross-validation remains scenario-specific for changes that affect boot, real-mode/protected-mode/long-mode transition, interrupt dispatch, timer IRQ, ATA PIO, port IO, or low-level driver behavior.

When Bochs cross-validation is unavailable, record why it was skipped, which QEMU, build, source-level, or manual checks were used instead, and what residual hardware-behavior risk remains.

## Preserved Contracts

Runtime smoke productization must not change kernel link addresses, BootInfo or handoff ABI, page-table assumptions, IDT vectors, IRQ EOI rules, syscall vector `0x80`, CR3 switching rules, smoke marker strings, or smoke-only user process boundaries. The image path remains the existing Legacy BIOS raw image with MBR/exFAT, `/boot/boot.bin`, root `kernel`, and IDE-compatible disk exposure; it does not require UEFI, OVMF, ESP/FAT, virtio, AHCI/SATA, NVMe, or a new storage driver.
