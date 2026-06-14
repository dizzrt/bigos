## ADDED Requirements

### Requirement: 用户态 syscall wrapper 只消费稳定 ABI
BigOS userland libc SHALL implement syscall wrappers only through the documented stable kernel/user ABI. Wrappers MUST NOT depend on kernel-private structs, interrupt frame layout details, scheduler internals, VFS internals, or x86_64 backend implementation headers beyond the explicitly documented register convention and shared ABI constants.

#### Scenario: wrapper 构建不包含内核私有实现
- **WHEN** userland libc wrappers are built for freestanding user programs
- **THEN** their public and private userland includes MUST resolve through freestanding-safe user ABI headers
- **AND** wrapper code MUST NOT include kernel-only process, VFS, interrupt, scheduler, or architecture backend implementation headers

#### Scenario: wrapper 只暴露已实现 syscall
- **WHEN** a libc header declares a syscall wrapper or POSIX-like function
- **THEN** the declared interface MUST correspond to an implemented and specified BigOS bounded syscall or libc behavior
- **AND** the declaration MUST NOT imply complete POSIX libc, hosted stdio, dynamic loading, threads, locale, or unsupported filesystem semantics

### Requirement: 用户态公共头文件 ABI 边界
BigOS userland public headers SHALL expose only the minimal constants, types, syscall wrappers, errno values, and libc declarations needed by the current bounded userland subset. Header organization MUST keep kernel-private implementation details out of user programs while preserving a stable freestanding include surface.

#### Scenario: 头文件只暴露 bounded subset
- **WHEN** a simple static C user program includes BigOS userland public headers
- **THEN** it MUST see declarations for supported wrappers and libc helpers
- **AND** it MUST NOT see declarations for unsupported POSIX families, internal kernel structs, backend-specific descriptor state, or unimplemented runtime facilities

#### Scenario: errno 来源保持一致
- **WHEN** kernel syscall code and userland libc wrappers reference errno constants
- **THEN** they MUST use a shared stable errno value source or a generated equivalent proven consistent with that source
- **AND** userland MUST NOT maintain a divergent errno numbering table

### Requirement: 已使用公共声明先补规格再隐藏
BigOS SHALL treat declarations already used by bundled user programs, smoke programs, crt0, or libc internals as existing userland ABI surface until they are explicitly reviewed. If such a declaration lacks precise OpenSpec coverage, the implementation work MUST first add or update the corresponding specification before retaining, moving, renaming, or hiding that declaration.

#### Scenario: 盘点发现已使用但规格不足的声明
- **WHEN** the header audit finds an implemented declaration used by bundled userland but only covered by broad or implicit requirements
- **THEN** the change MUST add explicit bounded specification coverage for that declaration before changing its visibility
- **AND** the implementation MUST NOT silently hide the declaration if doing so breaks an existing shell, smoke, libc, or packaged user-program path

#### Scenario: 当前 BigOS-specific 声明被显式归类
- **WHEN** implementation audits declarations such as `wait_status`, `bigos_readdir`, `brk_raw`, `mmap_anon`, `time_now`, `get_tick`, raw `syscall0`-`syscall6`, or non-minimum string helpers such as `strchr`
- **THEN** each declaration MUST be classified as public bounded ABI, libc-internal-only helper, compatibility umbrella export, or removal candidate
- **AND** public or compatibility exports MUST have matching specification and documentation before the task is complete

### Requirement: raw syscall primitive 暴露受限
BigOS userland libc MAY keep raw `syscall0` through `syscall6` primitives for libc-internal or explicitly documented BigOS-specific use, but those primitives MUST be treated as low-level ABI helpers rather than POSIX-like portable APIs. Their declarations MUST remain freestanding-safe, follow the documented register ABI exactly, and avoid implying support for arbitrary unsupported syscalls.

#### Scenario: libc 内部通过 raw primitive 调用 syscall
- **WHEN** libc wrappers call raw syscall primitives
- **THEN** the raw primitive MUST place the syscall number and arguments in the documented registers and return the raw kernel value from `rax`
- **AND** higher-level wrappers MUST remain responsible for errno translation unless the primitive is explicitly documented as returning raw kernel values

#### Scenario: raw primitive 不扩大公共兼容承诺
- **WHEN** user-facing headers expose raw syscall primitives directly or through an umbrella header
- **THEN** documentation MUST describe them as BigOS-specific low-level helpers
- **AND** the exposure MUST NOT imply POSIX `syscall(2)` compatibility, broad syscall stability, hosted libc support, or permission to bypass documented wrapper semantics for unsupported operations

### Requirement: BigOS-specific helper 声明保持有界
BigOS userland libc SHALL document and constrain BigOS-specific helper declarations that are not standard C/POSIX names but are part of the current bounded userland, including process wait status helpers, minimal directory enumeration, raw break query, restricted anonymous mapping, monotonic tick query, and wall-clock query helpers.

#### Scenario: helper 声明与能力边界匹配
- **WHEN** headers expose a BigOS-specific helper for wait status, directory enumeration, heap/mapping, tick, or wall-clock behavior
- **THEN** the helper MUST map to an implemented syscall or libc behavior already covered by the relevant bounded capability
- **AND** its documentation MUST state the bounded return value, errno behavior, and non-goals where they differ from POSIX or hosted libc

#### Scenario: helper 缺少规格时不新增使用面
- **WHEN** a BigOS-specific helper lacks explicit specification or documentation
- **THEN** new user programs MUST NOT expand dependence on that helper until the specification is added
- **AND** existing uses MAY remain only as compatibility or smoke consumers while the boundary cleanup resolves the declaration

### Requirement: libc ABI 文档与头文件一致
BigOS SHALL keep userland libc documentation, public headers, and wrapper behavior aligned for the supported bounded subset.

#### Scenario: 文档描述已声明接口
- **WHEN** docs describe a userland libc wrapper, errno behavior, or public header
- **THEN** the described interface MUST be declared in the userland public headers and implemented in userland libc
- **AND** unsupported behavior MUST be documented as a non-goal rather than exposed as a declaration

#### Scenario: 英中镜像保持同一 ABI 事实
- **WHEN** userland libc ABI documentation is updated under `docs/en`
- **THEN** the corresponding `docs/zh` mirror MUST be updated with the same syscall wrapper, errno, and bounded subset facts
