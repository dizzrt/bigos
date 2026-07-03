## ADDED Requirements

### Requirement: persistent /rw 提供有界 write-ahead journal
BigOS SHALL define a bounded write-ahead journal for the persistent `/rw` backend. The journal MUST live inside the persistent bigfs volume, MUST reserve exactly 32 filesystem blocks in the journal-capable format, MUST use deterministic transaction sequence metadata, and MUST be initialized only by the explicit persistent format path. This capability MUST NOT modify the read-only exFAT boot asset, the default RAM-backed `/rw` behavior, boot addresses, syscall numbers, or page-table layout.

#### Scenario: explicit format initializes journal area
- **WHEN** the persistent `/rw` format path initializes a supported empty persistent test disk
- **THEN** BigOS MUST initialize the superblock, root metadata, allocation metadata, and 32-block journal area before publishing persistent `/rw`
- **AND** the initialized journal area MUST be empty or checkpoint-clean according to the journal metadata

#### Scenario: incompatible journal format is rejected
- **WHEN** persistent mount validation sees a bigfs image without the required journal-capable format version, journal bounds, or journal checksum/sequence metadata
- **THEN** BigOS MUST reject persistent `/rw` publication or fall back according to the documented policy
- **AND** it MUST NOT auto-migrate, auto-format, reinterpret old bytes as journal records, or modify the read-only boot asset

### Requirement: filesystem mutations use journal-first transactions
BigOS SHALL commit persistent `/rw` filesystem mutations through journal-first transactions. A transaction MUST include enough bounded after-image records to describe every affected home-location data block, directory block, inode block, bitmap/free-space block, and required superblock/sequence metadata block. BigOS MUST write and synchronize the journal records before synchronizing the corresponding home-location updates.

#### Scenario: create publishes home metadata only after journal commit
- **WHEN** a persistent `/rw` create or mkdir operation allocates an inode, updates a parent directory, and changes allocation metadata
- **THEN** BigOS MUST write the affected after-image records to the journal and synchronize the journal commit marker before writing the corresponding inode, directory, and bitmap home blocks
- **AND** it MUST report success only after the ordered home-location updates and journal checkpoint/clear step complete

#### Scenario: file growth orders data before publishing size and mapping
- **WHEN** a persistent `/rw` write grows a regular file and updates file data, block mapping, size, timestamps, or allocation metadata
- **THEN** BigOS MUST include the affected file data and metadata blocks in the transaction or otherwise make the data durable before publishing the metadata home update
- **AND** it MUST NOT let an inode size or block mapping become durable while the referenced new data block is missing, stale, or uninitialized

#### Scenario: truncate and unlink preserve free-space ordering
- **WHEN** a persistent `/rw` truncate, unlink, rmdir, or restricted regular-file rename changes ownership of blocks, directory entries, or free-space metadata
- **THEN** BigOS MUST journal the affected metadata and data-ordering records before writing home-location changes
- **AND** released blocks MUST NOT become reusable in a durable state that can expose stale data or duplicate ownership

### Requirement: partial journal writes fail without durable success
BigOS SHALL make journal write failures deterministic and state-preserving. If any journal descriptor, record payload, checksum, sequence, or commit-marker write fails, the filesystem operation MUST return a deterministic error and MUST NOT report durable success for the attempted mutation.

#### Scenario: journal payload write fails
- **WHEN** the backing block device or request layer fails while writing a journal record for a persistent `/rw` transaction
- **THEN** BigOS MUST return a deterministic write-back error to the caller
- **AND** it MUST NOT clear the dirty or pending state needed for retry or diagnosis

#### Scenario: journal commit marker is not durable
- **WHEN** journal records were written but the commit marker cannot be synchronized successfully
- **THEN** BigOS MUST treat the transaction as not durably committed
- **AND** it MUST NOT write or report success for home-location updates that depend on that transaction

### Requirement: M15.1 journal path does not claim mount-time recovery
BigOS SHALL keep the M15.1 journaled write path distinct from mount-time recovery. The kernel MAY validate journal metadata at mount time, but it MUST NOT claim replay, discard, repair, or power-loss recovery until the recovery capability is specified and implemented separately.

#### Scenario: committed uncheckpointed journal is detected before recovery exists
- **WHEN** persistent mount validation detects a committed but not checkpointed journal transaction and no recovery implementation is available
- **THEN** BigOS MUST reject publication of that persistent volume as writable `/rw`
- **AND** it MUST NOT silently ignore the journal and publish the volume as clean writable state
- **AND** ordinary boot MAY fall back to RAM-backed `/rw` only if it records an explicit journal-needs-recovery diagnostic

#### Scenario: validation does not report crash recovery pass
- **WHEN** journal validation runs in M15.1
- **THEN** it MUST verify journal-first ordering and deterministic failure behavior
- **AND** it MUST NOT report mount-time replay, crash recovery, or power-loss recovery as passed

### Requirement: journal validation is reproducible and default-off
BigOS SHALL provide default-off validation for the persistent `/rw` journaled write path under the current x86_64 emulator validation environment. Validation MUST record toolchain, emulator, ROM/display, serial capture, and persistent disk availability; passed checks; skipped checks; and residual risk.

#### Scenario: journal smoke covers successful ordered commit
- **WHEN** journaling validation is enabled with the required toolchain, emulator, serial capture, and persistent test disk
- **THEN** the validation MUST exercise at least file create/write/fsync, directory metadata mutation, truncate or unlink, and restricted rename through the journal-first path
- **AND** it MUST confirm that successful operations remain visible through lookup, metadata queries, directory enumeration, and fd I/O after a clean validation boundary

#### Scenario: unavailable environment is recorded
- **WHEN** xmake, x86_64-elf toolchain, QEMU or Bochs, ROM/display support, serial capture, or the persistent disk image is unavailable
- **THEN** validation MUST be recorded as skipped or blocked with the missing dependency and residual risk
- **AND** it MUST NOT be reported as runtime-passed
