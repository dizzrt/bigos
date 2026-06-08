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
