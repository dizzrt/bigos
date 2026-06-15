## ADDED Requirements

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

BigOS SHALL continue to reject ELF images that require dynamic runtime support while preserving explicit metadata extension points for a future dynamic-linking capability.

#### Scenario: dynamic support requirement is deterministic failure

- **WHEN** an ELF image contains `PT_INTERP`, requires dynamic relocation, is a shared object, is an unsupported `ET_DYN` image, or requires a runtime dynamic loader
- **THEN** BigOS MUST reject the image with a deterministic loader or exec error
- **AND** it MUST NOT create a runnable process with unresolved dynamic runtime requirements

#### Scenario: future loader metadata is inert

- **WHEN** loader metadata includes reserved fields for future interpreter, shared-object, relocation, or runtime-linker handoff data
- **THEN** current BigOS MUST leave those fields inert and unavailable to user execution
- **AND** no reserved metadata field may cause extra mappings, widened permissions, or dynamic-loader entry without a later explicit capability

### Requirement: initial stack and argument layout is bounded

BigOS SHALL require the loader/exec path to construct initial user stack, `argv`, and `envp` data within the committed runtime VM layout using bounded sizes and deterministic failure behavior.

#### Scenario: argument stack setup succeeds within bounds

- **WHEN** exec prepares `argv` and `envp` for a bounded static user image
- **THEN** BigOS MUST place the initial stack data inside the allowed user stack/runtime argument area with correct user permissions and alignment for the established ABI
- **AND** entry into ring3 MUST use the committed stack pointer and entry point from the runtime image description

#### Scenario: argument stack setup failure rolls back

- **WHEN** argument count, environment count, string length, stack size, alignment, or address arithmetic exceeds the bounded loader policy
- **THEN** BigOS MUST fail exec deterministically before publishing the new image
- **AND** the old process image MUST remain active unless the process has entered a documented fatal exec path
