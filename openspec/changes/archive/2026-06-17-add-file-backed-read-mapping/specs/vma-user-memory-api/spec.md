## ADDED Requirements

### Requirement: VMA 支持 file-backed 只读 backing 类型

BigOS SHALL extend the bounded VMA model with a read-only file-backed backing type that records a backing file reference and a starting file offset in addition to range, permissions, purpose, growth policy, and materialization accounting. VMA range validation, user-range copy validation, `fork` duplication, exec replacement, and process teardown MUST recognize file-backed VMAs and preserve their read-only ownership.

#### Scenario: file-backed VMA 记录文件引用与偏移

- **WHEN** a read-only file-backed mapping is published
- **THEN** the VMA MUST record the backing file reference, the starting file offset, a read-only permission set, and lazy materialization accounting sufficient to distinguish materialized from unmaterialized pages
- **AND** the VMA MUST remain within the supported user low-half range and avoid kernel higher-half, direct-map, KVMEM, and recursive self-mapping ranges

#### Scenario: 用户范围校验识别 file-backed 区间

- **WHEN** syscall handling validates a user range that overlaps a read-only file-backed VMA
- **THEN** BigOS MUST allow read access covered by present user-accessible mappings or an equivalent safe-copy path, and MUST reject write access to the read-only file-backed range
- **AND** it MUST NOT treat a present PTE alone as authorization for write access to a read-only file-backed page

#### Scenario: teardown 释放 file-backed VMA 不误删共享缓存

- **WHEN** a process holding file-backed VMAs exits or is reaped, or its image is replaced by exec
- **THEN** BigOS MUST release the file-backed VMA metadata and the process-owned page-table entries consistently
- **AND** it MUST NOT corrupt or prematurely free shared read-only page/buffer cache state still referenced by other processes
