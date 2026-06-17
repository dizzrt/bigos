## ADDED Requirements

### Requirement: file-backed 只读页跨独立进程共享

BigOS SHALL extend bounded read-only file-backed mappings so independently created compatible mappings can share already-materialized read-only pages. This includes explicit file mapping requests and compatible read-only ELF text/rodata mappings produced by `exec`. The extension MUST preserve the existing private VMA policy surface: each process keeps its own VMA range, permissions, file offset, and accounting, while the physical page may be shared when the backing key and permissions are compatible.

#### Scenario: 独立 mmap 复用同一文件页

- **WHEN** two unrelated processes request read-only file-backed mappings for the same readable regular file page with compatible page-aligned offsets and read-only permissions
- **THEN** BigOS MUST allow the second process's first read fault to reuse the already materialized shared frame
- **AND** each process MUST keep independent VMA metadata and may unmap or exit without removing the other process's VMA

#### Scenario: exec 复用同一 ELF text/rodata 页

- **WHEN** two unrelated processes `exec` the same static ELF image and the loader creates compatible read-only file-backed VMAs for text or rodata load pages
- **THEN** BigOS MUST allow the later process's first fault on the same file page to reuse the already materialized shared frame
- **AND** each process MUST keep independent address-space layout, VMA metadata, and exec lifecycle state

#### Scenario: VMA 仍是权限来源

- **WHEN** a process accesses an address whose PTE references a shared read-only file frame
- **THEN** BigOS MUST still validate the access against that process's VMA permissions and range
- **AND** another process's compatible mapping MUST NOT grant wider permissions or extend this process's VMA range

### Requirement: file-backed 物化保留 tail zero-fill 与 mappable extent

BigOS SHALL keep the existing file-backed materialization semantics for shared pages: in-file bytes MUST be read through the page/buffer cache, bytes beyond the mappable file extent within the final mapped page MUST be zero-filled, and accesses beyond the VMA or mappable extent MUST fail deterministically. A shared tail page MUST NOT expose stale bytes from another object or previous frame owner.

#### Scenario: 共享尾页零填充一致

- **WHEN** two processes map and read the same final file-backed page whose page frame extends beyond the backing file length
- **THEN** both processes MUST observe identical file bytes for the in-file portion and zero bytes for the beyond-EOF portion
- **AND** BigOS MUST NOT expose uninitialized frame contents or data from a different backing object

#### Scenario: 超出 mappable extent 不借共享页成功

- **WHEN** a CPL3 access targets an address beyond the file-backed VMA or beyond the backing file's mappable extent
- **THEN** BigOS MUST reject the access through the documented user fault path
- **AND** it MUST NOT satisfy the access by aliasing a nearby shared page entry

### Requirement: file-backed 共享页不扩大 POSIX mmap 语义

BigOS SHALL keep shared read-only file-backed pages within the bounded mapping contract. The capability MUST NOT imply writable file mappings, write-back through `msync`, full `MAP_SHARED`, fixed overwrite mappings, broad file-backed `mmap`, dynamic linking, shared libraries, or shared anonymous mappings. Exec participation is limited to compatible read-only static ELF text/rodata pages.

#### Scenario: writable 或 shared-writable 请求仍失败

- **WHEN** a user mapping request asks for writable file-backed mapping, shared writable semantics, W+X permissions, fixed overwrite behavior, or unsupported POSIX flags
- **THEN** BigOS MUST reject the request deterministically
- **AND** it MUST NOT publish a shared read-only page entry as a partial success for that unsupported request
