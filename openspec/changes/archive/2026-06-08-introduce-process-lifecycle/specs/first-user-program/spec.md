## MODIFIED Requirements

### Requirement: 最小用户进程模型

BigOS SHALL provide a minimal single-core user process abstraction that owns a user address-space root, user entry point, user stack range, associated kernel execution context, lifecycle state, PID, parent/child linkage, and exit status. This abstraction SHALL be backed by the normal process lifecycle core and SHALL remain sufficient to run one bounded first user program smoke without implying `fork`, COW, signals, SMP, fd/VFS, demand paging, or general POSIX policy. After the process is terminated or faulted, BigOS SHALL expose a safe lifecycle boundary that allows a later teardown path to release the process-owned address space, user pages, dynamic page-table pages, and kernel stack without freeing resources still active on the current return path.

#### Scenario: 创建首个用户进程

- **WHEN** kernel initialization or a default-off smoke path creates the first user process
- **THEN** the process record MUST contain a stable PID allocated by the normal process lifecycle core, a derived user address-space root, a user entry address, a user stack top/range, lifecycle state, and parent/root ownership metadata
- **AND** creation MUST occur in non-interrupt context and MUST NOT allocate process objects from IRQ handlers
- **AND** the process record MUST identify the resources that are owned by the process and eligible for later teardown

#### Scenario: 进程退出状态被记录

- **WHEN** the first user process calls the exit syscall or is terminated by a controlled user fault
- **THEN** BigOS MUST record a deterministic terminated state and exit code or fault reason in the normal process lifecycle state
- **AND** BigOS MUST NOT immediately reclaim the currently active kernel stack or process object on the same return path
- **AND** BigOS MUST make the process eligible for parent-visible wait status or later safe teardown once execution has switched to a safe kernel context

#### Scenario: 进程资源在安全上下文释放

- **WHEN** a terminated or faulted first user process is reaped from a safe non-IRQ kernel context
- **THEN** BigOS MUST release the process-owned user address-space root, user code/data/stack pages, dynamic user page-table pages, and process kernel stack
- **AND** it MUST defer or fail safely if the target kernel stack or user root is still active

### Requirement: ELF loader 复用首个用户程序 runtime 边界

BigOS SHALL keep the flat embedded first-user-program smoke independent from filesystem-backed ELF loading while allowing ELF user-program runtime and later general exec paths to reuse the normal process lifecycle, ring3 entry, syscall, user fault, and safe teardown boundaries established for user processes. Reuse SHALL NOT make the embedded smoke depend on block devices, filesystems, user ELF artifacts, fd/VFS, demand paging, or user-space libc.

#### Scenario: embedded smoke remains filesystem-independent

- **WHEN** the existing flat embedded first user program smoke is enabled without the ELF user program smoke
- **THEN** BigOS MUST continue obtaining that user program from its embedded or build-time-packaged artifact
- **AND** it MUST NOT require ATA PIO probing, exFAT mount, user ELF path lookup, filesystem reads, fd/VFS, or general exec file lookup to validate the embedded smoke path

#### Scenario: ELF smoke reuses process runtime safely

- **WHEN** the ELF user program smoke or a later general exec path creates a user process image
- **THEN** BigOS MAY reuse the normal process lifecycle core, derived user address-space root, TSS/RSP0 setup, `iretq` ring3 entry, `SYS_WRITE`/`SYS_EXIT`, parent-visible status, and user fault termination mechanisms
- **AND** all ELF-owned pages, dynamic user page-table pages, temporary loader buffers, process kernel stack, PID/table state, and process state MUST remain identifiable for safe teardown

#### Scenario: smoke configuration is explicit

- **WHEN** build configuration enables user program smokes
- **THEN** the flat embedded smoke and filesystem-backed ELF smoke MUST be independently selectable or otherwise documented as mutually exclusive
- **AND** normal boot with both smokes disabled MUST NOT run user program smoke entry paths as an implicit requirement
- **AND** disabling both smokes MUST NOT require hiding the normal process lifecycle core from compilation
