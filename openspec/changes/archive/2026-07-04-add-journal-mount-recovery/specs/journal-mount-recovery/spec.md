## ADDED Requirements

### Requirement: persistent /rw 挂载时执行 journal recovery
BigOS SHALL run a bounded journal recovery phase before publishing a journal-capable persistent bigfs volume as writable `/rw`. The recovery phase MUST classify the on-disk journal state, MUST complete committed transactions by replaying durable after-image records to their home-location blocks, MUST clear or checkpoint the journal only after successful recovery, and MUST NOT expose the persistent filesystem through VFS until recovery reaches a clean publishable state.

#### Scenario: clean journal mounts without replay
- **WHEN** persistent `/rw` mount validation sees a supported journal-capable bigfs volume whose journal is checkpoint-clean
- **THEN** BigOS MUST publish the persistent volume as writable `/rw`
- **AND** it MUST NOT rewrite unrelated home-location blocks or alter the read-only boot asset

#### Scenario: committed transaction is replayed before publication
- **WHEN** mount validation sees a complete committed but not checkpointed journal transaction with valid sequence, checksum, record count, target bounds, and commit marker
- **THEN** BigOS MUST replay every journal after-image record to its home-location block before publishing persistent writable `/rw`
- **AND** it MUST synchronize the replayed home-location blocks before marking the journal checkpoint-clean
- **AND** the recovered filesystem state MUST expose the committed file, directory, metadata, and allocation changes through ordinary VFS lookup, fd I/O, and directory enumeration

#### Scenario: replay is idempotent after repeated mount attempts
- **WHEN** a previous recovery attempt wrote some home-location blocks but failed before checkpointing or clearing the journal
- **THEN** the next mount attempt MUST treat the same committed journal transaction as replayable
- **AND** repeating the replay MUST converge to the same home-location block contents without duplicating allocation ownership or directory entries

### Requirement: uncommitted journal state is discarded only when safe
BigOS SHALL discard an incomplete journal transaction only when the journal lacks a durable commit marker and the journal metadata is otherwise parseable enough to prove the state is an uncommitted partial transaction. Discarding MUST leave the journal checkpoint-clean and MUST NOT publish any mutation that depended on the missing commit marker.

#### Scenario: partial transaction without commit marker is cleared
- **WHEN** mount validation sees journal descriptor or payload records with valid bounds and sequence but no durable commit marker
- **THEN** BigOS MUST treat the transaction as not committed
- **AND** it MUST clear or checkpoint the journal before publishing persistent writable `/rw`
- **AND** it MUST NOT replay the partial records as a successful filesystem mutation

#### Scenario: partial journal clear failure prevents publication
- **WHEN** BigOS determines that an uncommitted partial journal may be discarded but the clear or checkpoint write cannot be synchronized
- **THEN** BigOS MUST reject persistent writable `/rw` publication for that volume
- **AND** ordinary boot MAY fall back to RAM-backed `/rw` only if it records an explicit journal-recovery-failed diagnostic

### Requirement: corrupt or unsupported journal states fail closed
BigOS SHALL reject persistent writable `/rw` publication when journal recovery cannot prove a clean, safely discarded, or successfully replayed state. Unsupported versions, invalid journal bounds, target blocks outside the recoverable bigfs region, checksum mismatch, sequence discontinuity, malformed record counts, conflicting commit/checkpoint markers, or recovery I/O failure MUST be treated as non-publishable persistent states.

#### Scenario: corrupt committed journal is rejected
- **WHEN** mount validation sees a committed journal transaction whose checksum, sequence, record count, target block bounds, or payload length is invalid
- **THEN** BigOS MUST reject persistent writable `/rw` publication
- **AND** it MUST NOT silently discard the transaction or replay a best-effort subset
- **AND** ordinary boot MAY fall back to RAM-backed `/rw` only if it records an explicit corrupt-journal diagnostic

#### Scenario: replay write failure keeps persistent volume unpublished
- **WHEN** recovery starts replaying a valid committed transaction but any home-location write, sync, checkpoint, or clear operation fails
- **THEN** BigOS MUST keep the persistent volume unpublished as writable `/rw`
- **AND** it MUST leave enough journal state for a later mount attempt to retry recovery or report the same failure deterministically

### Requirement: journal recovery validation is reproducible and default-off
BigOS SHALL provide default-off validation for persistent `/rw` journal recovery under the current x86_64 emulator validation environment. Validation MUST record toolchain, emulator, ROM/display, serial capture, persistent disk availability, constructed journal states, passed checks, skipped checks, and residual risk.

#### Scenario: recovery smoke covers replay discard and reject
- **WHEN** journal recovery validation is enabled with the required toolchain, emulator, serial capture, and persistent test disk support
- **THEN** the validation MUST exercise at least clean mount, committed replay, uncommitted discard, corrupt journal reject, and recovery failure fallback diagnostics
- **AND** it MUST verify recovered file contents, directory entries, metadata visibility, and allocation consistency after a successful replay

#### Scenario: unavailable recovery validation environment is recorded
- **WHEN** xmake, x86_64-elf toolchain, QEMU or Bochs, ROM/display support, serial capture, or persistent disk image support is unavailable
- **THEN** validation MUST be recorded as skipped or blocked with the missing dependency and residual risk
- **AND** it MUST NOT be reported as runtime-passed
