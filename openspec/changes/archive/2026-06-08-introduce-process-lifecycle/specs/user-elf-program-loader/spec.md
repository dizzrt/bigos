## MODIFIED Requirements

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
