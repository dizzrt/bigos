## ADDED Requirements

### Requirement: runtime filesystem descriptions distinguish journaled writes from recovery
BigOS SHALL describe active `/rw` filesystem behavior according to the selected backend and implemented durability stage. RAM-backed `/rw` remains current-runtime-only; persistent `/rw` may provide journaled write-ahead commit ordering when mounted with the journal-capable format; mount-time replay/discard and crash recovery remain separate future capabilities until implemented.

#### Scenario: user-visible diagnostics name journaled persistent backend accurately
- **WHEN** diagnostics, validation output, docs, or shell-visible metadata describe the active persistent `/rw` backend after M15.1
- **THEN** they MUST distinguish journaled write-ahead commit ordering from mount-time recovery
- **AND** they MUST NOT claim that non-clean shutdown recovery, replay, discard, or repair is available

#### Scenario: RAM-backed runtime behavior remains current-session-only
- **WHEN** `/rw` is using the RAM-backed current-session backend
- **THEN** BigOS MUST keep existing runtime filesystem behavior and failure semantics
- **AND** documentation or validation MUST NOT imply that journaled persistent ordering applies to RAM-backed state

### Requirement: filesystem validation records journaling stage boundaries
BigOS SHALL record journaling validation results separately from runtime filesystem maturity, persistent clean-sync, and future recovery validation. Passing M15.1 journaling validation MUST mean the journal-first write path and deterministic failure behavior were exercised, not that crash recovery has passed.

#### Scenario: journaling validation passes without recovery claim
- **WHEN** default-off journaling validation completes successfully
- **THEN** the result MUST identify the checked journal-first ordering and clean validation boundary
- **AND** it MUST record mount-time replay/discard recovery as not covered by this validation

#### Scenario: recovery-dependent check is unavailable
- **WHEN** a validation scenario would require rebooting after an unclean shutdown and replaying or discarding a journal
- **THEN** M15.1 validation MUST mark that scenario as out of scope, skipped, or blocked
- **AND** it MUST NOT report the scenario as passed by ordinary clean reboot readback
