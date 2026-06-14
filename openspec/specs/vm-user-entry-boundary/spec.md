## Purpose

定义 BigOS 当前 x86_64 可运行 backend 下 VM policy、页表 materialization、地址空间 activation、用户态入口和 fault handling 的所有权边界，确保边界整理不改变既有低层 ABI 或扩大 backend/POSIX 承诺。

## Requirements

### Requirement: VM 与用户态入口边界所有权明确

BigOS SHALL define explicit ownership boundaries between core virtual-memory policy, page-table materialization, address-space activation, user-entry mechanics, and architecture-specific fault dispatch under the current x86_64 runnable backend.

#### Scenario: 评审 VM 到用户入口控制流

- **WHEN** 变更触及 VMA policy、user page mapping、address-space activation、`execve` user entry、syscall return、user-mode fault handling 或 safe teardown 路径
- **THEN** 评审记录必须能够区分 core-owned VM policy、page-table materialization state、architecture-owned activation/user-entry mechanics 和 process-owned lifecycle/reaper policy
- **AND** portable kernel policy MUST NOT require direct knowledge of x86_64 CR3 encoding, GDT/TSS layout, raw interrupt-frame offsets, `iretq` frame details, or page-table bit encodings outside the documented boundary

#### Scenario: 保留低层 ABI 与内存布局假设

- **WHEN** VM/user-entry boundary cleanup 完成
- **THEN** BigOS MUST preserve existing boot addresses, linker layout, higher-half kernel mapping, direct map, KVMEM layout, recursive/self-mapping address, syscall vector, IDT vector behavior, user stack assumptions, disk layout, and current CR3 switching semantics
- **AND** any required change to those assumptions MUST be split into a separate OpenSpec change with explicit bootability and ABI risk

### Requirement: VM policy 与页表 materialization 分离

BigOS SHALL treat VMA/process metadata as the source of user virtual-memory policy and page-table state as the currently materialized translation state. User mappings, user-buffer validation, demand-zero, COW, stack growth, and teardown MUST preserve consistency between the two layers.

#### Scenario: VMA policy authorizes user mappings

- **WHEN** kernel code materializes a user mapping for exec image pages, heap, anonymous mappings, stack, demand-zero, or COW
- **THEN** the operation MUST confirm that a compatible VMA or documented process VM policy authorizes the target range before publishing a present user PTE
- **AND** the resulting page-table permissions MUST NOT grant access wider than the VMA or process VM policy permits

#### Scenario: 页表存在不绕过 VMA 策略

- **WHEN** syscall user-buffer validation, fault recovery, or user-copy handling observes a present user page-table entry
- **THEN** BigOS MUST still reject or fault the access if the relevant VMA/process policy is absent or lacks the requested read, write, or execute permission
- **AND** it MUST NOT treat a present PTE alone as sufficient authorization for core user-memory access

#### Scenario: rollback 保持 VMA 与页表一致

- **WHEN** exec preparation, `brk`, anonymous mapping, stack growth, demand-zero, or COW materialization fails after allocating VMA metadata, physical pages, or intermediate page-table pages
- **THEN** BigOS MUST roll back the current operation or terminate through a documented safe teardown path before returning success
- **AND** it MUST NOT leave a successful return with VMA policy and page-table materialization disagreeing about the affected range

### Requirement: 地址空间 activation 是架构机制边界

BigOS SHALL separate address-space creation, mapping, activation, active-root restoration, and teardown. Core process logic MAY request semantic activation of a user address space, but architecture-owned code MUST contain raw CR3 writes, architecture TLB effects, and register-level entry requirements.

#### Scenario: core 请求激活用户地址空间

- **WHEN** process or exec code needs to run a user process under a prepared user address-space root
- **THEN** it MUST consume a semantic activation boundary or documented architecture helper rather than open-coding x86_64 CR3 writes or assuming raw register state in unrelated core policy
- **AND** the activated root MUST preserve kernel higher-half mappings required for syscall, exception, IRQ, direct-map, KVMEM, scheduler, and diagnostics paths

#### Scenario: safe teardown 不释放活动资源

- **WHEN** a process exits, faults, or replaces its image
- **THEN** BigOS MUST defer freeing the active user root, process kernel stack, user-owned low-half page tables, and VMA metadata until a safe kernel context no longer depends on them
- **AND** teardown MUST preserve borrowed kernel high-half mappings and MUST NOT reclaim boot-stage, direct-map, KVMEM, recursive/self-mapping, or shared kernel page tables as user-owned resources

### Requirement: 用户态入口边界只暴露核心语义

BigOS SHALL expose user-entry and user-return behavior to core subsystems through semantic boundaries that describe entering user mode, returning from syscall/exception paths, or terminating a user process, while keeping x86_64 frame, descriptor, segment, TSS/RSP0, and assembly restore details in architecture-owned implementation.

#### Scenario: 进入用户态

- **WHEN** BigOS prepares to enter or re-enter a user process
- **THEN** core code MUST provide the semantic user context, address-space ownership, user instruction pointer, user stack, and process state required by the documented boundary
- **AND** x86_64 `iretq` frame construction, selector details, GDT/TSS/RSP0 mechanics, and register restore order MUST remain in architecture-owned or explicitly low-level entry code

#### Scenario: 用户入口整理不承诺新 backend parity

- **WHEN** implementation, documentation, or validation describes the user-entry boundary
- **THEN** it MUST state or preserve that the current runnable backend remains x86_64 Legacy BIOS/MBR/exFAT
- **AND** it MUST NOT claim UEFI runtime parity, non-x86 runtime parity, SMP readiness, complete HAL behavior, dynamic linking support, or broader POSIX compatibility as a result of this boundary cleanup

### Requirement: fault handling 分流用户恢复与内核诊断

BigOS SHALL preserve explicit fault classification between user-mode recoverable cases, user-mode termination cases, and kernel-mode diagnostic or panic paths. Recoverable user page faults MUST remain bounded to documented VMA-backed demand-zero, COW, stack-growth, or equivalent implemented cases.

#### Scenario: 可恢复用户页错误

- **WHEN** a CPL3 page fault matches an implemented VMA-backed demand-zero, COW, stack-growth, or documented user-memory recovery case
- **THEN** BigOS MAY materialize or adjust the required mapping through the VM policy and page-table materialization boundary
- **AND** recovery MUST preserve VMA permissions, page-table permissions, process state, and address-space ownership invariants before returning to user mode

#### Scenario: 不可恢复用户 fault

- **WHEN** a CPL3 fault is outside the documented recoverable user cases or violates VMA/process policy
- **THEN** BigOS MUST terminate or mark the current process through the documented user-fault lifecycle path
- **AND** it MUST arrange safe teardown rather than freeing the active user root, current kernel stack, or process object on an unsafe return path

#### Scenario: 内核 fault 不被当作用户恢复

- **WHEN** a fault occurs in CPL0 or in a kernel-owned diagnostic, syscall, IRQ, scheduler, or teardown path
- **THEN** BigOS MUST preserve the existing kernel diagnostic or panic behavior unless a separate change explicitly defines a kernel recovery path
- **AND** the fault handler MUST NOT incorrectly resume the kernel path using user-mode VMA recovery logic

### Requirement: 边界验证匹配触及范围

BigOS SHALL validate VM/user-entry boundary work with checks matched to the touched layer and SHALL record unavailable low-level validation explicitly.

#### Scenario: Spec 或文档-only 边界工作

- **WHEN** a change only updates OpenSpec artifacts, roadmap-level planning, or architecture documentation without changing runtime control flow
- **THEN** OpenSpec status or validation checks and targeted consistency searches MUST be sufficient validation
- **AND** runtime emulator smoke MAY be skipped with the documentation-only scope recorded

#### Scenario: runtime 边界工作

- **WHEN** a change modifies C++ headers, C++ sources, assembly stubs, page-table helpers, address-space activation, user-entry code, syscall return, or fault dispatch behavior
- **THEN** validation MUST include the narrowest useful cross-toolchain build or explicitly record missing `x86_64-elf-gcc`/xmake dependencies
- **AND** when the environment supports it, validation SHOULD include QEMU headless smoke for the touched userland, demand-paging, COW, syscall, or user-entry behavior
- **AND** Bochs MAY be used for early boot or hardware-behavior cross-validation, and unavailable Bochs validation MUST be recorded as residual manual/cross-validation risk rather than a default automation blocker

#### Scenario: targeted consistency search 覆盖低层假设

- **WHEN** VM/user-entry boundary cleanup is reviewed
- **THEN** targeted consistency search or equivalent review notes MUST confirm that boot/linker/page-table/vector/user ABI assumptions were not silently moved or widened
- **AND** if implementation exposes a repeatable blind spot around core code directly consuming x86_64-private CR3, frame, descriptor, or PTE details, a narrow source-level check MUST be added or the residual risk MUST be recorded
