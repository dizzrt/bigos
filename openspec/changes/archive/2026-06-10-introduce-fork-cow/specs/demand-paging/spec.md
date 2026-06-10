## ADDED Requirements

### Requirement: COW 写错误分支

BigOS SHALL extend the unified user page-fault handler with a copy-on-write write-fault branch that is distinct from the existing protection-violation kill rule. When a CPL3 fault has the present (protection-violation) bit set but targets a read-only, COW-marked, writable anonymous-backed page, BigOS MUST treat it as a copy-on-write split candidate rather than an unconditional permission-violation kill. All other present-bit faults (non-COW protection violations) MUST keep the existing deterministic kill semantics.

#### Scenario: present 位 COW 页不再直接 kill

- **WHEN** a CPL3 write fault sets the present bit on a read-only page that carries the COW marker and lies in a writable anonymous-backed VMA
- **THEN** BigOS MUST route the fault to the copy-on-write split branch instead of terminating the process as a permission violation
- **AND** after the split or in-place re-enable the faulting instruction MUST resume successfully

#### Scenario: 非 COW present 违例仍 kill

- **WHEN** a CPL3 fault sets the present bit on a page that is not COW-marked, or requests access incompatible with the covering VMA permissions
- **THEN** BigOS MUST preserve the existing deterministic kill through the documented user fault path
- **AND** it MUST NOT convert a genuine protection violation into a successful copy-on-write materialization
