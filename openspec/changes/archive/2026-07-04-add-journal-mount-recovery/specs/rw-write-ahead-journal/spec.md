## MODIFIED Requirements

### Requirement: M15.1 journal path does not claim mount-time recovery
BigOS SHALL keep the M15.1 journaled write path distinct from mount-time recovery while allowing M15.2 to consume the same journal format for recovery. The kernel MAY validate journal metadata at mount time before recovery is available, but once the M15.2 recovery capability is implemented it MUST use the journal's sequence metadata, checksums, record bounds, commit markers, and checkpoint state to decide whether the persistent `/rw` volume can be replayed, safely discarded, or rejected. M15.1-only builds or configurations MUST NOT claim replay, discard, repair, or power-loss recovery.

#### Scenario: committed uncheckpointed journal is detected before recovery exists
- **WHEN** persistent mount validation detects a committed but not checkpointed journal transaction and no recovery implementation is available
- **THEN** BigOS MUST reject publication of that persistent volume as writable `/rw`
- **AND** it MUST NOT silently ignore the journal and publish the volume as clean writable state
- **AND** ordinary boot MAY fall back to RAM-backed `/rw` only if it records an explicit journal-needs-recovery diagnostic

#### Scenario: committed uncheckpointed journal is handed to recovery when available
- **WHEN** persistent mount validation detects a committed but not checkpointed journal transaction and the M15.2 recovery implementation is available
- **THEN** BigOS MUST classify and validate the transaction through the journal recovery path before publishing persistent writable `/rw`
- **AND** it MUST publish the persistent volume only after successful replay and checkpoint/clear, or reject it if recovery cannot prove a clean consistent state

#### Scenario: validation reports recovery only after replay or discard is implemented
- **WHEN** journal validation runs without the M15.2 recovery implementation enabled
- **THEN** it MUST verify journal-first ordering and deterministic failure behavior
- **AND** it MUST NOT report mount-time replay, crash recovery, or power-loss recovery as passed

#### Scenario: validation distinguishes write ordering from recovery
- **WHEN** journal validation runs with the M15.2 recovery implementation enabled
- **THEN** it MUST report journal-first write ordering and mount-time recovery as separate validation outcomes
- **AND** a passing write-ordering check MUST NOT mask a failed replay, discard, reject, or recovery diagnostic check
