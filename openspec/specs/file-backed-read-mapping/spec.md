## Purpose

定义 BigOS 有界只读 file-backed 用户映射能力：通过专用映射请求把可读常规文件的一段页对齐范围登记为私有只读 VMA，首次读访问时经 page/buffer cache 按需物化文件内容，并保持写访问、越界访问和不可阻塞上下文装入的确定性失败边界。该能力不提供 writable/write-back file mapping、`MAP_SHARED` 跨进程可写共享、`mprotect`、固定覆盖映射、swap 或完整 POSIX `mmap`/`munmap` 语义。

## Requirements

### Requirement: file-backed 只读用户映射请求

BigOS SHALL provide a bounded read-only file-backed user mapping request. The request MUST take a readable regular-file descriptor, a page-aligned file offset, and a page-aligned length, and on success MUST publish a non-overlapping read-only private file-backed VMA covering a page-aligned user low-half range registered as lazily backed. The capability SHALL NOT implement writable/write-back file mapping, `MAP_SHARED` cross-process writable sharing, `mprotect`, fixed-overwrite mapping, swap, or full POSIX `mmap`/`munmap` semantics.

#### Scenario: 合法只读映射建立 file-backed VMA

- **WHEN** a process requests a read-only mapping with a readable regular-file fd, a page-aligned offset, and a page-aligned length that fits in the supported user low-half range without overlapping existing VMAs
- **THEN** BigOS MUST publish a read-only private file-backed VMA recording the backing file reference and starting file offset, register its range as lazily backed, and return the mapped user address range without eagerly reading every page
- **AND** the published VMA MUST be read-only and non-executable unless an explicit read-only executable policy allows otherwise

#### Scenario: 非法或不支持的映射请求被拒绝

- **WHEN** a mapping request supplies a non-readable fd, a non-regular-file fd, an unaligned offset or length, write or W+X permissions, shared/writable semantics, a kernel-space address, an arithmetic overflow, or a range that collides with existing VMAs
- **THEN** BigOS MUST reject the request deterministically with a negative error
- **AND** it MUST NOT publish partial VMA metadata or a partial user mapping as a successful operation

### Requirement: file-backed 页经缓存按需物化

BigOS SHALL materialize file-backed pages lazily on first access through the page/buffer cache from a process context that permits blocking. On a CPL3 not-present read fault within a file-backed VMA, BigOS MUST compute the file offset from the VMA file offset and the faulting page position, read the covering file block(s) through the existing page/buffer cache read path, install a read-only user page-table entry, and advance the VMA materialization accounting. A page region beyond the backing file length but within the mapped range MUST be zero-filled; cache load MUST NOT occur from non-blocking contexts.

#### Scenario: 首次读访问命中正确文件内容

- **WHEN** a CPL3 read access faults on a not-present page within a file-backed VMA in a blocking-capable process context, and the page is within the backing file length
- **THEN** BigOS MUST read the covering file block(s) through the page/buffer cache, install a read-only non-executable user page-table entry holding the file content, and advance the VMA materialization accounting
- **AND** the faulting user instruction MUST resume and observe the file content

#### Scenario: 文件尾页零填充

- **WHEN** a CPL3 read fault targets a file-backed page that partially extends beyond the backing file length but remains within the mapped VMA range
- **THEN** BigOS MUST materialize the page with file content for the in-file portion and zero-fill the remainder
- **AND** the materialized page MUST stay read-only

#### Scenario: 不可阻塞上下文不发起缓存装入

- **WHEN** a file-backed materialization would require a cache load from IRQ context, a scheduler critical section, a preemption-disabled region, or another non-blocking path
- **THEN** BigOS MUST fail deterministically or enter the documented diagnostic path
- **AND** it MUST NOT issue blocking block I/O for the materialization

### Requirement: file-backed 越界与写访问确定性失败

BigOS SHALL terminate the current user process deterministically through the documented user fault path when a CPL3 access targets a file-backed range outside the mapped VMA or the backing file's mappable extent, or requests write access to a read-only file-backed page. BigOS MUST NOT convert such faults into a successful materialization and MUST NOT enter copy-on-write for file-backed read-only pages.

#### Scenario: 越界访问确定性 kill

- **WHEN** a CPL3 access targets an address outside the mapped file-backed VMA range or beyond the backing file's mappable extent
- **THEN** BigOS MUST terminate the current user process through the documented user fault path
- **AND** it MUST NOT silently materialize a page for the out-of-range access

#### Scenario: 对只读 file-backed 页的写访问 kill

- **WHEN** a CPL3 write access targets a read-only file-backed page, whether the page is present or not-present
- **THEN** BigOS MUST terminate the current user process through the documented user fault path as a permission violation
- **AND** it MUST NOT enter copy-on-write or read-only materialization for the write

### Requirement: file-backed 映射跨 fork 共享只读缓存

BigOS SHALL duplicate file-backed read-only VMAs on `fork` as metadata, preserving the backing file reference, file offset, range, permissions, and materialization accounting, and MUST keep already-materialized read-only file pages shared rather than deep-copied. Unmaterialized portions MUST be carried as metadata and materialized later through the unified page-fault path in whichever process first accesses them.

#### Scenario: fork 复制 file-backed VMA 不深拷贝

- **WHEN** `fork` duplicates a process holding a file-backed read-only mapping with some materialized pages
- **THEN** BigOS MUST copy the file-backed VMA metadata including file reference, offset, and materialization accounting, and keep the materialized read-only pages shared between parent and child
- **AND** unmaterialized portions MUST be duplicated as metadata without forcing materialization

### Requirement: file-backed 只读映射验证可复现

BigOS SHALL provide reproducible validation for file-backed read-only mapping behind a default-off switch that emits fixed COM1/VGA markers and does not delete or alter the existing default boot markers or smoke matrix.

#### Scenario: 默认关闭的 smoke 覆盖映射行为

- **WHEN** the kernel is built with the file-backed read mapping validation switch enabled
- **THEN** the smoke MUST exercise mapping creation, first-access materialization observing correct file content, tail-page zero-fill, out-of-range deterministic kill, and write-to-read-only deterministic kill, and emit a deterministic COM1/VGA pass/fail marker
- **AND** with no smoke switches the file-backed mapping smoke MUST stay off and the existing smoke matrix MUST remain available

#### Scenario: build 和 smoke 验证记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` cross-toolchain build, relevant `uv run pytest` source-level checks, strict OpenSpec validation for this change, and any QEMU headless serial-marker smoke used for file-backed mapping behavior
- **AND** it MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened

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
