## ADDED Requirements

### Requirement: 进程对象拥有 VMA 集合

BigOS SHALL extend the normal process lifecycle core so each user process image owns a bounded VMA collection. The process object MUST distinguish the active committed VMA collection from any staging VMA collection used during exec or image replacement.

#### Scenario: process publication includes VMA state

- **WHEN** a process is published into the process table with a runnable user image
- **THEN** BigOS MUST associate the process with a committed VMA collection that describes the runnable image, heap boundary, anonymous mappings, and stack policy
- **AND** syscall user-buffer validation, page fault handling, and teardown MUST resolve VMA state from the current process image rather than global singleton state

#### Scenario: process creation failure releases VMA state

- **WHEN** process creation fails after allocating VMA metadata but before publishing a runnable process
- **THEN** BigOS MUST release the VMA metadata along with owned user pages, page-table pages, kernel stack, fd table, and loader buffers allocated by the failed attempt
- **AND** it MUST NOT publish a process table entry that references a partial VMA collection

### Requirement: exec 以 staging VMA commit

BigOS SHALL build a new VMA collection in staging state during exec and publish it only when the new user image can be committed. The old VMA collection MUST remain valid until commit succeeds or the old process image is deliberately terminated.

#### Scenario: exec commit swaps VMA atomically

- **WHEN** exec validation, segment mapping, stack setup, heap setup, and fd close-on-exec handling have all succeeded
- **THEN** BigOS MUST atomically publish the new VMA collection as the process committed image state before entering the new user instruction stream
- **AND** subsequent user-buffer checks and fault handling MUST use the new committed VMA collection

#### Scenario: exec rollback preserves old VMA

- **WHEN** exec fails before commit and the old image remains runnable
- **THEN** BigOS MUST preserve the old process VMA collection unchanged
- **AND** it MUST release all staging VMA metadata and owned memory from the failed exec attempt

### Requirement: exit/fault/reaper preserves VMA safety

BigOS SHALL integrate VMA ownership with exit, user fault, zombie, wait, and safe reaper state transitions. Unsafe active paths MUST NOT free the current process VMA collection before control has moved to a safe kernel context.

#### Scenario: exit does not free active VMA immediately

- **WHEN** a user process invokes exit while running on its process kernel stack or under its user address-space root
- **THEN** BigOS MUST record exit state and arrange safe reaping without immediately freeing the active VMA collection on that return path
- **AND** parent-visible wait status MUST remain observable independent of VMA cleanup

#### Scenario: user fault marks process before VMA cleanup

- **WHEN** a user-mode page fault or invalid user-buffer operation terminates the current process
- **THEN** BigOS MUST record a deterministic fault reason and transition the process to faulted, zombie, or reap-pending state before VMA teardown
- **AND** final VMA cleanup MUST occur only through the documented safe reaper boundary
