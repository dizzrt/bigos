## ADDED Requirements

### Requirement: ELF loader 复用首个用户程序 runtime 边界

BigOS SHALL keep the flat embedded first-user-program smoke independent from filesystem-backed ELF loading while allowing a separate ELF user-program runtime to reuse the minimal `Process`, ring3 entry, syscall, user fault, and safe teardown boundaries established for the first user program. Reuse SHALL NOT make the embedded smoke depend on block devices, filesystems, or user ELF artifacts.

#### Scenario: embedded smoke remains filesystem-independent

- **WHEN** the existing flat embedded first user program smoke is enabled without the ELF user program smoke
- **THEN** BigOS MUST continue obtaining that user program from its embedded or build-time-packaged artifact
- **AND** it MUST NOT require ATA PIO probing, exFAT mount, user ELF path lookup, or filesystem reads to validate the embedded smoke path

#### Scenario: ELF smoke reuses process runtime safely

- **WHEN** the new ELF user program smoke is enabled
- **THEN** BigOS MAY reuse the existing minimal process creation, derived user address-space root, TSS/RSP0 setup, `iretq` ring3 entry, `SYS_WRITE`/`SYS_EXIT`, and user fault termination mechanisms
- **AND** all ELF-owned pages, dynamic user page-table pages, temporary loader buffers, process kernel stack, and process state MUST remain identifiable for safe teardown

#### Scenario: smoke configuration is explicit

- **WHEN** build configuration enables user program smokes
- **THEN** the flat embedded smoke and filesystem-backed ELF smoke MUST be independently selectable or otherwise documented as mutually exclusive
- **AND** normal boot with both smokes disabled MUST NOT compile or run user program runtime code as an implicit requirement
