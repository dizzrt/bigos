## ADDED Requirements

### Requirement: persistent /rw clean-sync advances to journal-first commit
BigOS SHALL advance persistent `/rw` clean-sync commits to a journal-first write path when the mounted volume uses the journal-capable bigfs format. Successful `fsync`, explicit sync, cache eviction of protected persistent blocks, and high-level metadata commit paths MUST honor the write-ahead journal ordering before reporting durable success. This requirement MUST NOT claim mount-time journal replay, crash recovery, power-loss safety, automatic repair, or persistence for unsynchronized dirty state.

#### Scenario: fsync uses journal-first durable path
- **WHEN** a process modifies persistent `/rw` file data and metadata and calls `fsync` from a blockable process context
- **THEN** BigOS MUST synchronize the required journal transaction before synchronizing the corresponding home-location data and metadata blocks
- **AND** `fsync` MUST return success only after the journaled transaction and home-location updates complete according to the ordered commit plan

#### Scenario: journaled commit failure does not report clean-sync success
- **WHEN** journal record write-back, journal commit marker write-back, home-location write-back, or journal checkpoint/clear write-back fails
- **THEN** BigOS MUST return a deterministic write-back error
- **AND** it MUST NOT describe the attempted filesystem state as durably committed or clean-sync eligible

### Requirement: journal-capable persistent format is explicit
BigOS SHALL require explicit formatting or a compatible journal-capable persistent bigfs format before enabling the journaled persistent `/rw` path. The journal-capable format MUST reserve exactly 32 filesystem blocks for the write-ahead journal. Mount validation MUST reject incompatible format versions, invalid journal bounds, invalid journal sequence metadata, or contradictions between the journal metadata and filesystem metadata.

#### Scenario: old clean-sync image is not silently upgraded
- **WHEN** normal boot attempts to mount a persistent bigfs image created before the journal-capable format
- **THEN** BigOS MUST reject persistent `/rw` publication or fall back according to the documented policy
- **AND** it MUST NOT silently rewrite the superblock, auto-migrate metadata, or publish the old image as journal-protected storage

#### Scenario: explicit format publishes journal-capable /rw
- **WHEN** the explicit persistent format path succeeds on a supported persistent test disk
- **THEN** BigOS MUST publish persistent `/rw` using the journal-capable format
- **AND** later synchronized filesystem mutations MUST use the journal-first commit path

### Requirement: persistent /rw reports unfinished journal state conservatively
BigOS SHALL treat unfinished journal state conservatively until mount-time recovery is implemented. If mount validation detects a journal state that requires replay or discard, the persistent volume MUST NOT be published as clean writable storage. Ordinary boot MAY fall back to RAM-backed `/rw` only when it records an explicit diagnostic that persistent `/rw` was withheld because journal recovery is required.

#### Scenario: mount sees journal requiring replay
- **WHEN** persistent mount validation sees a committed transaction that has not been checkpointed into home-location blocks
- **THEN** BigOS MUST reject publication of that persistent volume as writable `/rw`
- **AND** it MUST NOT expose paths, metadata, or file contents from that volume as recovered state
- **AND** ordinary boot fallback to RAM-backed `/rw`, if used, MUST be diagnostically distinguishable from a successful persistent mount

#### Scenario: mount sees partial uncommitted journal
- **WHEN** persistent mount validation sees partial journal records without a durable commit marker
- **THEN** BigOS MUST treat the volume according to the documented pre-recovery policy
- **AND** it MUST NOT claim that discard or repair has been completed unless a later recovery capability implements it
