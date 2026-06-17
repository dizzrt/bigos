## ADDED Requirements

### Requirement: file-backed 只读物化分支

BigOS SHALL extend the unified user page-fault handler with a file-backed read-only materialization branch that is a controlled exception to the existing rule that terminates CPL3 faults on non-anonymous backing lacking a recovery policy. When a CPL3 not-present read fault hits a registered-but-unmaterialized page of a read-only file-backed VMA, in a context that permits blocking, BigOS MUST materialize the page from the backing file through the page/buffer cache with read-only user permissions rather than terminating the process. All write faults on read-only file-backed pages and all faults outside a recoverable VMA MUST keep the existing deterministic kill semantics.

#### Scenario: not-present 读缺页进入 file-backed 分支

- **WHEN** a CPL3 not-present read fault targets a page within a read-only file-backed VMA, the access is read-only and within the backing file's mappable extent, and the context permits blocking
- **THEN** BigOS MUST route the fault to the file-backed read-only materialization branch, read the covering file block(s) through the page/buffer cache, install a read-only non-executable user page-table entry, and advance the VMA materialization accounting
- **AND** the faulting instruction MUST resume successfully observing the file content

#### Scenario: file-backed 写缺页或越界仍 kill

- **WHEN** a CPL3 fault requests write access to a read-only file-backed page, targets an address outside the mapped file-backed range, or occurs in a non-blocking context
- **THEN** BigOS MUST preserve deterministic termination through the documented user fault path
- **AND** it MUST NOT convert the fault into a successful file-backed materialization or enter copy-on-write

#### Scenario: 内核态缺页仍 panic

- **WHEN** a page fault occurs in CPL0 / kernel context
- **THEN** BigOS MUST preserve the existing kernel diagnostic/panic behavior
- **AND** it MUST NOT attempt file-backed materialization for kernel-mode faults
