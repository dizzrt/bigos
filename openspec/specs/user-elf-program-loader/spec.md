## Purpose

Define the default-off user ELF64 program loader for BigOS: reading a bounded user ELF file through the kernel read-only filesystem stack, validating ELF headers and program headers, mapping `PT_LOAD` segments with explicit user permissions, binding the loaded image to the minimal process runtime, and validating the filesystem-backed ring3 smoke path. This capability does not introduce general `exec`, dynamic linking, user libc, file descriptor syscalls, demand paging, `mmap`/`brk`, COW, signals, preemptive scheduling, writable filesystems, or a broad multi-process model.

## Requirements

### Requirement: 用户 ELF 文件来源

BigOS SHALL provide a default-off user ELF program load path that reads a bounded ELF64 user program file from the kernel read-only filesystem stack after block device, memory management, syscall entry, and process runtime prerequisites are initialized. The load path SHALL run only in ordinary non-IRQ kernel context and SHALL NOT depend on hosted OS file IO, bootloader-only exFAT helpers, or normal boot being configured for user program execution.

#### Scenario: 从约定路径读取用户 ELF

- **WHEN** the ELF user program smoke is enabled and the raw image contains the configured user ELF file path
- **THEN** BigOS MUST mount or reuse the read-only kernel filesystem path, lookup the configured absolute path, read the file through the bounded FS API into kernel-owned memory, and pass the bytes to the ELF loader
- **AND** the read MUST occur outside IRQ handler context and before entering ring3

#### Scenario: 文件缺失或读取失败

- **WHEN** the configured user ELF file is missing, exceeds the bounded loader size, cannot be read, or returns a filesystem/block error
- **THEN** BigOS MUST emit a deterministic `BIGOS_USER_ELF_` failure marker or enter the unified panic path
- **AND** BigOS MUST NOT enter ring3 with an absent, partial, or unchecked ELF image

#### Scenario: smoke disabled leaves boot unchanged

- **WHEN** the ELF user program smoke build option is disabled
- **THEN** normal kernel boot MUST NOT require ATA PIO probing, exFAT mount, user ELF lookup, or ring3 user execution from this load path

### Requirement: ELF64 header and program header validation

BigOS SHALL validate the user ELF image before creating executable user mappings. The loader MUST accept only bounded x86_64 little-endian ELF64 executable images with sane program header metadata and MUST reject images that require unsupported runtime features.

#### Scenario: ELF identity validates

- **WHEN** the loader receives bytes for a user ELF image
- **THEN** it MUST verify the ELF magic, class, endian, version, machine, executable type, header size, program header offset, program header entry size, and program header count before using any program header fields
- **AND** all header arithmetic MUST be checked for overflow and file bounds

#### Scenario: unsupported ELF feature is rejected

- **WHEN** the ELF image contains unsupported type, machine, class, interpreter, dynamic linking requirement, malformed alignment, invalid entry point, absent loadable segment, or program headers outside the file
- **THEN** the loader MUST reject the image with a deterministic loader error
- **AND** it MUST NOT create a runnable `Process`

#### Scenario: user virtual addresses are bounded

- **WHEN** a `PT_LOAD` segment or entry point is validated
- **THEN** its virtual address range MUST stay in the supported user low-half address range, avoid wraparound, avoid kernel higher-half/direct-map/KVMEM/self-mapping ranges, and avoid overlap with the configured user stack range
- **AND** the entry point MUST lie inside a present executable load segment

### Requirement: ELF segments map with explicit user page attributes

BigOS SHALL map ELF `PT_LOAD` segments into a derived user address-space root with explicit user page attributes derived from ELF segment flags. File-backed bytes SHALL be copied from the ELF file, memory-only bss bytes SHALL be zero-filled, and no mapping SHALL be published with unsafe or ambiguous permissions.

#### Scenario: executable segment is mapped user executable

- **WHEN** a validated `PT_LOAD` segment has execute permission and does not require writable permission
- **THEN** BigOS MUST allocate user-owned physical pages, map them present and user-accessible, clear NX for executable pages, copy the segment file bytes to the correct page offsets, and zero-fill any remaining memory bytes
- **AND** the mapping MUST NOT grant writable permission unless ELF flags explicitly require it

#### Scenario: writable data and bss are mapped NX

- **WHEN** a validated `PT_LOAD` segment has writable data or bss bytes
- **THEN** BigOS MUST allocate user-owned physical pages, map them present, user-accessible, writable as needed, and no-execute
- **AND** all bytes in `p_memsz` beyond `p_filesz` MUST be zero-filled before entering user mode

#### Scenario: unsafe segment permissions are rejected

- **WHEN** the loader would need to create a user page that is both writable and executable, overlaps another segment with incompatible permissions, or aliases kernel-owned physical memory
- **THEN** BigOS MUST reject the ELF image or fail the load path deterministically
- **AND** it MUST NOT enter ring3 with a W+X or ambiguous user mapping

### Requirement: ELF process runtime and lifecycle binding

BigOS SHALL create or replace a user process image for a successfully loaded ELF image, bind its entry point, user stack, optional bounded `argv`/`envp` layout, address-space root, owned segment pages, and kernel execution context to the normal process lifecycle core, and arrange safe teardown on exit, fault, exec failure, or load failure.

#### Scenario: ELF process enters ring3

- **WHEN** the ELF image has been validated and all required segment, stack, and initial argument mappings are present
- **THEN** BigOS MUST prepare the process entry point from the ELF header, set the initial user stack pointer to mapped user stack memory, activate the process user address-space root through the controlled process run path, and enter CPL3 through the established ring3 entry mechanism
- **AND** the syscall gate, process PID/table state, and TSS/RSP0 kernel-stack return mechanism MUST be initialized before user execution

#### Scenario: load failure releases owned resources

- **WHEN** ELF loading or exec preparation fails after allocating a process, user page, dynamic page-table page, kernel buffer, argument buffer, or kernel stack
- **THEN** BigOS MUST release or mark for safe release every owned resource that is no longer reachable by a runnable process
- **AND** it MUST not free borrowed high-half kernel page tables or resources still active on the current stack/CR3 path

#### Scenario: user exit and user fault use safe reaper boundary

- **WHEN** the ELF user program calls `SYS_EXIT` or is terminated by a controlled CPL3 fault
- **THEN** BigOS MUST record the terminated or faulted state in the normal process lifecycle, preserve deterministic exit/fault diagnostics, wake any eligible parent waiters, and use the safe reaper boundary before freeing process-owned segment pages, user stack, dynamic user page tables, user PML4 root, and process kernel stack

#### Scenario: general exec reuses ELF loader

- **WHEN** a process lifecycle path invokes general exec for a bounded ELF64 `ET_EXEC` image
- **THEN** BigOS MUST reuse the same ELF validation, segment permission, user address bound, W+X rejection, and safe resource ownership rules as the ELF smoke loader
- **AND** exec-specific `argv`/`envp` setup MUST be bounded and committed only after the new user stack layout is valid

### Requirement: ELF user program validation is reproducible

BigOS SHALL provide reproducible source-level, build-level, image-level, process-lifecycle-level, and optional emulator validation for the ELF user program load and exec paths. Validation MUST distinguish current-change failures from local emulator or serial observability blockers.

#### Scenario: source checks cover loader invariants

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover ELF header bounds, program header bounds, user virtual address bounds, segment overlap rejection, W+X rejection, page attribute conversion, bss zero-fill, no hidden CR3 switch during passive load preparation, safe resource release on load failure, process table publication after commit, and bounded `argv`/`envp` stack setup
- **AND** checks MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, exception/IRQ gate privilege, and EOI semantics are not moved or widened by this change

#### Scenario: build and image validation are recorded

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` build, relevant `uv run pytest` or source-level checks, user ELF image packaging or raw-image validation, process lifecycle smoke coverage, and strict OpenSpec validation for this change
- **AND** if Bochs or QEMU runtime smoke cannot observe `BIGOS_USER_ELF_`, `BIGOS_USER_EXIT`, wait/exit, or exec markers due to emulator, ROM, serial/VGA oracle, image lock, or interaction limitations, validation MUST record the unavailable dependency and remaining bootability risk

### Requirement: ELF loader produces runtime image description

BigOS SHALL make the bounded ELF loader produce a runtime image description before process-visible commit. The description MUST include validated loadable segment ranges, segment permissions, entry point, initial stack and argument layout, heap seed, VMA purposes/backing, resource ownership, and unsupported dynamic-linking feature state.

#### Scenario: static ELF image prepares complete runtime description

- **WHEN** the loader accepts a bounded static ELF64 executable image
- **THEN** BigOS MUST prepare a complete runtime image description for the exec or process creation path before entering ring3
- **AND** the description MUST be sufficient to create compatible VMAs, user mappings, initial stack state, heap metadata, and lifecycle ownership records

#### Scenario: incomplete image description rejects execution

- **WHEN** the loader cannot describe segment permissions, stack layout, argument layout, heap seed, VMA ownership, or resource cleanup for an image
- **THEN** BigOS MUST reject the image or fail exec deterministically before publishing the new process image
- **AND** it MUST release or safely retain any resources allocated during failed preparation

### Requirement: loader rejects unsupported dynamic images while preserving future hooks

BigOS SHALL accept, only on a default-off bounded dynamic-link load path, an `ET_DYN` main executable containing exactly one `PT_INTERP` program header by loading the named user-space interpreter and handing control to it, while continuing to reject all dynamic-runtime features outside that bounded subset. 当动态链接路径关闭时，BigOS MUST 保持既有行为：对 `PT_INTERP`、动态重定位需求、共享对象、`ET_DYN` 执行或运行时动态加载器一律确定性拒绝。即使动态路径启用，超出有界子集的动态特性（TLS、`IFUNC`、符号版本、多 `PT_INTERP`、无界 `DT_NEEDED`）MUST 仍被确定性拒绝；既有静态 `ET_EXEC` 路径与其 bounded 限制 MUST 保持不变。

#### Scenario: 动态路径关闭时仍确定性拒绝

- **WHEN** 动态链接构建开关关闭，且某 ELF 镜像包含 `PT_INTERP`、需要动态重定位、是共享对象、是 `ET_DYN` 镜像或需要运行时动态加载器
- **THEN** BigOS MUST 以确定性 loader/exec 错误拒绝该镜像
- **AND** BigOS MUST NOT 创建带未解析动态运行时需求的可运行进程

#### Scenario: 动态路径启用时接受有界动态镜像

- **WHEN** 动态链接路径启用，且镜像是含恰好一个 `PT_INTERP` 的有界 `ET_DYN` 可执行程序，解释器存在且为受支持的有界 `ET_DYN`
- **THEN** BigOS MUST 按确定性基址加载主镜像与解释器并以解释器入口进入 ring3
- **AND** BigOS MUST 保留越界、重叠、对齐与 W^X 安全校验，仅按基址偏移调整地址校验

#### Scenario: 动态路径启用时仍拒绝超界特性

- **WHEN** 动态路径启用，但镜像或其依赖包含 TLS 重定位、`IFUNC`、符号版本、多于一个 `PT_INTERP`，或 `DT_NEEDED`/共享对象/重定位条目超出有界上限
- **THEN** BigOS 或其用户态解释器 MUST 以确定性失败拒绝，MUST NOT 进入 ring3 或带着未解析引用执行主程序

#### Scenario: future loader metadata is inert

- **WHEN** loader metadata includes reserved fields for future interpreter, shared-object, relocation, or runtime-linker handoff data that are not part of the enabled bounded dynamic-link path
- **THEN** current BigOS MUST leave those fields inert and unavailable to user execution
- **AND** no reserved metadata field may cause extra mappings, widened permissions, or dynamic-loader entry without the explicit bounded dynamic-link path

### Requirement: initial stack and argument layout is bounded

BigOS SHALL require the loader/exec path to construct initial user stack, `argv`, `envp`, and (on the dynamic load path) a bounded auxiliary vector within the committed runtime VM layout using bounded sizes and deterministic failure behavior. auxv MUST 在 `envp` 的 NULL 终止符之后追加并以 `AT_NULL` 终止，MUST NOT 改变 `argc`/`argv`/`envp` 的相对布局或破坏既有静态 crt0 的读取假设。

#### Scenario: argument stack setup succeeds within bounds

- **WHEN** exec prepares `argv` and `envp` for a bounded static user image
- **THEN** BigOS MUST place the initial stack data inside the allowed user stack/runtime argument area with correct user permissions and alignment for the established ABI
- **AND** entry into ring3 MUST use the committed stack pointer and entry point from the runtime image description

#### Scenario: 动态镜像初始栈追加有界 auxv

- **WHEN** exec 在动态加载路径为 `ET_DYN` + `PT_INTERP` 主镜像准备初始栈
- **THEN** BigOS MUST 在 `envp` 的 NULL 终止符之后写入有界 auxv（至少 `AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL`）于已提交 runtime VM 布局内
- **AND** 既有静态 crt0 对该追加式 auxv MUST 保持透明，仍能正确读取 `argc`/`argv`/`envp`

#### Scenario: argument stack setup failure rolls back

- **WHEN** argument count, environment count, string length, auxv size, stack size, alignment, or address arithmetic exceeds the bounded loader policy
- **THEN** BigOS MUST fail exec deterministically before publishing the new image
- **AND** the old process image MUST remain active unless the process has entered a documented fatal exec path
