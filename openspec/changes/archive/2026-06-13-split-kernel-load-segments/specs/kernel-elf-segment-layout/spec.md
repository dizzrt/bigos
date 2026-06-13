## MODIFIED Requirements

### Requirement: Kernel ELF LOAD segments have separated permissions

BigOS kernel ELF SHALL use separate `PT_LOAD` program headers for executable text, read-only data, and writable data so that no loadable segment is simultaneously writable and executable. The layout MUST preserve the existing x86_64 higher-half kernel base, `_start` entry path, and boot handoff ABI. The repository linker script MUST keep the implementation synchronized with this contract rather than relying on a single permissive `RWE` kernel segment.

#### Scenario: 构建产物不包含 RWX LOAD segment

- **WHEN** kernel 使用项目链接脚本完成链接
- **THEN** ELF program header table MUST NOT contain any `PT_LOAD` segment with both write and execute permissions
- **AND** linker output MUST NOT report `LOAD segment with RWX permissions`

#### Scenario: 链接脚本声明分离的 PHDR 权限

- **WHEN** 审查 kernel `link.lds`
- **THEN** it MUST define separate loadable program headers for text, rodata, and data permission classes
- **AND** no kernel `PT_LOAD` program header in `link.lds` MUST use `FLAGS(7)` or equivalent read/write/execute permissions

#### Scenario: 可执行启动代码位于 RX segment

- **WHEN** 检查 kernel ELF 的 `_start` entry point 和 `.bigos`、`.init`、`.text`、`.fini` sections
- **THEN** 它们 MUST be covered by a readable and executable `PT_LOAD` segment
- **AND** that segment MUST NOT have write permission

#### Scenario: 只读数据位于只读 segment

- **WHEN** 检查 `.rodata`、`.rodata1`、只读 `.eh_frame_hdr` 和只读 `.eh_frame` sections
- **THEN** 它们 MUST be covered by a readable `PT_LOAD` segment
- **AND** that segment MUST NOT have write or execute permission

#### Scenario: 可写运行时数据位于 RW segment

- **WHEN** 检查 `.ctors`、`.dtors`、`.data`、`.4k_area` 和 `.bss` sections
- **THEN** 它们 MUST be covered by a readable and writable `PT_LOAD` segment
- **AND** that segment MUST NOT have execute permission

### Requirement: Segment boundaries preserve boot layout compatibility

BigOS kernel ELF segment splitting SHALL preserve the documented x86_64 boot layout assumptions. Segment boundary alignment is allowed to increase kernel image size, but MUST NOT change the higher-half base, kernel entry ABI, boot info handoff contract, or early memory consumer interpretation of kernel memory extent.

#### Scenario: Higher-half base remains stable

- **WHEN** 检查拆分后的 `PT_LOAD` virtual addresses
- **THEN** every loadable segment MUST remain at or above `0xffffffff80000000`
- **AND** the first kernel load virtual address exposed through boot metadata MUST remain `0xffffffff80000000`

#### Scenario: Entry point remains inside executable LOAD

- **WHEN** bootloader validates the ELF entry point
- **THEN** the entry point MUST fall inside a loaded executable `PT_LOAD` segment
- **AND** bootloader MUST continue to jump to the validated ELF entry point rather than a hard-coded fallback

#### Scenario: Segment boundaries are page-aligned

- **WHEN** the linker script transitions between text, rodata, and data permission classes
- **THEN** the resulting segment boundaries MUST be aligned to 4 KiB pages
- **AND** no single 4 KiB page MUST contain contents that require both executable and writable permissions

### Requirement: Kernel segment layout validation is reproducible

BigOS SHALL provide deterministic validation for the kernel ELF segment layout using the cross build and ELF inspection tools, with emulator smoke used when the local Bochs environment can reliably observe boot markers.

#### Scenario: Static ELF validation

- **WHEN** the segment layout change is implemented
- **THEN** validation MUST include a cross build and an ELF program-header inspection
- **AND** the recorded inspection MUST show separate LOAD permissions for text, rodata, and data without any `RWE` load segment

#### Scenario: Source-level regression guard

- **WHEN** source-level validation runs for the kernel ELF segment layout
- **THEN** it MUST check the linker script does not map all kernel sections to one `RWE PT_LOAD`
- **AND** it MUST check the expected text, rodata, and data permission classes remain represented in `link.lds`

#### Scenario: Boot smoke validation or explicit gap

- **WHEN** Bochs, ROM configuration, image generation, and serial marker observation are available
- **THEN** validation MUST include a bounded boot smoke that reaches an existing kernel marker
- **AND** if emulator observation is unavailable, validation MUST record the exact missing dependency or oracle limitation and the remaining bootability risk

#### Scenario: Documentation records non-goals

- **WHEN** the segment layout change is completed
- **THEN** architecture documentation MUST describe the kernel ELF permission layout
- **AND** documentation MUST state that runtime page-table W^X enforcement is out of scope for this change
- **AND** documentation MUST state that `.ctors/.dtors` remain in the writable data segment for this change
- **AND** documentation MUST record linker boundary symbols as the preferred future direction for kernel page-permission enforcement
