## ADDED Requirements

### Requirement: 共享只读页以稳定 backing key 复用

BigOS SHALL provide a bounded shared read-only materialized-page table for file-backed user mappings. A shared page key MUST include a stable VFS/backend object identity, a page-aligned file offset, and immutable read-only mapping attributes sufficient to prevent writable, executable-widened, or cross-object aliasing. The object identity MUST distinguish backend or mount identity and backend-local object id; it MUST NOT use a process-local `vfs::File *` pointer as the cross-open identity. The table MUST use its own bounded capacity and MUST fail deterministically when it cannot retain the backing object, allocate metadata, or reserve capacity.

#### Scenario: 第二个进程复用已物化只读页

- **WHEN** one process has materialized a read-only file-backed page and another process maps the same backing identity at the same page-aligned file offset with compatible read-only permissions
- **THEN** BigOS MUST reuse the existing shared physical frame instead of reading a second copy from the backing file
- **AND** both processes MUST receive read-only user PTEs referencing that frame

#### Scenario: 不兼容 key 不共享

- **WHEN** two mappings differ by backing identity, page-aligned file offset, writable permission, unsupported execute widening, or another immutable mapping attribute
- **THEN** BigOS MUST NOT alias them through the same shared read-only page entry
- **AND** it MUST either materialize a distinct allowed read-only page or reject the unsupported request deterministically

#### Scenario: 共享表容量耗尽

- **WHEN** a page fault needs a new shared read-only entry but the bounded shared table cannot allocate or reserve a slot
- **THEN** BigOS MUST fail the materialization deterministically through the existing user fault or negative-error boundary
- **AND** it MUST NOT publish a PTE or VMA accounting state that claims the shared page is present

#### Scenario: 独立 open 同一对象仍共享

- **WHEN** two processes independently open the same regular backing object and map the same page-aligned offset through distinct `vfs::File` objects
- **THEN** BigOS MUST key sharing by the VFS/backend object identity rather than by the two `File` pointer values
- **AND** both mappings MAY reuse the same shared read-only frame when permissions and immutable attributes are compatible

### Requirement: 共享只读页发布遵守引用计数顺序

BigOS SHALL retain shared read-only frames with explicit reference accounting before publishing any user PTE that points at them. A successful PTE installation MUST have a matching frame reference, and every failed publish path MUST drop only the references it acquired. The shared directory's own retained frame reference MUST remain live while the shared page can be found by future faults.

#### Scenario: 命中共享页后发布 PTE

- **WHEN** a file-backed read fault finds an existing shared read-only page entry
- **THEN** BigOS MUST increment or retain the frame reference before installing the process-local read-only PTE
- **AND** if PTE installation fails, it MUST drop that newly acquired reference before returning failure

#### Scenario: 新装入页先登记再发布

- **WHEN** a file-backed read fault loads a page from the page/buffer cache because no compatible shared entry exists
- **THEN** BigOS MUST initialize the frame contents, establish the shared entry reference, and only then publish the process-local read-only PTE
- **AND** any failure before PTE publication MUST release the frame, backing reference, and metadata acquired by that attempt

### Requirement: 写访问共享只读页确定性失败

BigOS SHALL treat write access to a shared read-only file-backed page as a permission fault. The handler MUST NOT enter copy-on-write, MUST NOT create a private writable copy, and MUST NOT write back to the backing file or page/buffer cache as part of the fault.

#### Scenario: present 共享只读页写入失败

- **WHEN** a CPL3 write targets a present PTE backed by a shared read-only file page
- **THEN** BigOS MUST terminate the current user process through the documented user fault path or return the existing deterministic fault result
- **AND** the shared frame contents and other processes' mappings MUST remain unchanged

#### Scenario: not-present 共享只读 VMA 写入失败

- **WHEN** a CPL3 write targets a not-present page inside a shared-capable read-only file-backed VMA
- **THEN** BigOS MUST reject the access as a permission violation
- **AND** it MUST NOT materialize the page, enter COW, or publish a writable PTE

### Requirement: 共享只读映射验证可复现

BigOS SHALL provide reproducible validation for shared read-only mapping behavior behind a default-off switch. Validation MUST cover explicit file-backed mapping sharing, exec text/rodata sharing, permission failure, lifecycle release, and preservation of existing boot, syscall, page-table, and TLB boundaries.

#### Scenario: 默认关闭的共享只读 smoke 覆盖多进程行为

- **WHEN** the shared read-only mapping validation switch is enabled
- **THEN** the smoke MUST exercise two processes mapping the same file page, confirm they observe identical content from one shared frame, confirm write access fails deterministically, and confirm one process unmap or exit does not invalidate the other process's read-only mapping
- **AND** with no smoke switches the validation MUST stay off and the existing default boot path MUST remain unchanged

#### Scenario: exec text/rodata 共享验证

- **WHEN** the shared read-only mapping validation switch runs two exec-created processes from the same static ELF image
- **THEN** their compatible read-only text or rodata pages MUST be eligible for the same shared read-only frame reuse as explicit file-backed mappings
- **AND** writable data or bss pages from the executable MUST remain private and MUST NOT enter the shared read-only table

#### Scenario: 验证记录边界不变

- **WHEN** implementation completes
- **THEN** validation MUST record strict OpenSpec validation, the narrowest useful xmake build, relevant source-level checks, and any QEMU headless serial-marker smoke used
- **AND** it MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, register ABI, and exception/IRQ EOI semantics are not moved or widened
