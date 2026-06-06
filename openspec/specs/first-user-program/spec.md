## Purpose

Define the first bounded user-program runtime for BigOS: a minimal single-core
user process abstraction, an embedded or build-time-packaged user image loader,
controlled x86_64 ring3 entry, minimal `write`/`exit` syscalls from CPL3, user
fault termination semantics, and reproducible validation. This capability is
limited to the first user-program smoke path and does not introduce fork/exec,
wait, signals, demand paging, filesystems, broad syscall coverage, SMP, or
general multi-process scheduling fairness.

## Requirements

### Requirement: 最小用户进程模型

BigOS SHALL provide a minimal single-core user process abstraction that owns a user address-space root, user entry point, user stack range, associated kernel execution context, lifecycle state, and exit status. This abstraction SHALL be sufficient to run one bounded first user program and SHALL NOT imply fork/exec/wait, signals, SMP, or general multi-process fairness.

#### Scenario: 创建首个用户进程

- **WHEN** kernel initialization or a default-off smoke path creates the first user process
- **THEN** the process record MUST contain a stable process id or equivalent bounded identity, a derived user address-space root, a user entry address, a user stack top/range, and a lifecycle state
- **AND** creation MUST occur in non-interrupt context and MUST NOT allocate process objects from IRQ handlers

#### Scenario: 进程退出状态被记录

- **WHEN** the first user process calls the exit syscall or is terminated by a controlled user fault
- **THEN** BigOS MUST record a deterministic terminated state and exit code or fault reason
- **AND** BigOS MUST NOT immediately reclaim the currently active kernel stack or process object on the same return path

### Requirement: 第一个用户程序加载

BigOS SHALL load a minimal first user program from an embedded or build-time-packaged image and map its code, data, BSS, and stack into the process user address space with explicit user page attributes.

#### Scenario: 用户程序镜像来自构建产物

- **WHEN** the first user program smoke is enabled
- **THEN** the kernel MUST obtain the user program from an embedded image, linked object, generated symbol range, or equivalent build-time packaged artifact
- **AND** the load path MUST NOT depend on kernel filesystem, block-device, hosted OS file IO, or bootloader-only exFAT helpers

#### Scenario: 用户代码和数据使用正确页属性

- **WHEN** the loader maps user program segments
- **THEN** executable user code pages MUST be present, user-accessible, and not NX
- **AND** writable user data, BSS, and stack pages MUST be present, user-accessible, writable as needed, and NX
- **AND** kernel higher-half, direct map, KVMEM, and self-mapping layout constants MUST remain unchanged

#### Scenario: 加载失败受控诊断

- **WHEN** the embedded image is malformed, too large for the bounded smoke layout, or cannot be mapped
- **THEN** BigOS MUST emit a deterministic `BIGOS_USER_` failure marker or enter the unified panic path
- **AND** the failure path MUST NOT continue into ring3 with a partially mapped process

### Requirement: 用户地址空间激活和 ring3 进入

BigOS SHALL provide a controlled x86_64 ring3 entry path for the first user process by activating its user address-space root, preparing required user selectors and kernel return stack state, and entering the user entry point through an explicit `iretq` frame or equivalent architecture-defined transition.

#### Scenario: 地址空间在进程运行路径中激活

- **WHEN** BigOS starts the first user process
- **THEN** it MUST activate the process user address-space root only from the controlled process run path
- **AND** the active root MUST preserve kernel higher-half accessibility for syscall, exception, interrupt, and diagnostic paths

#### Scenario: ring3 进入帧包含用户上下文

- **WHEN** BigOS transfers control to the first user program
- **THEN** the transition frame MUST contain the user RIP, user code selector, RFLAGS, user RSP, and user data/stack selector required to enter CPL3
- **AND** the user stack pointer MUST point to mapped user stack memory

#### Scenario: 内核栈返回机制已准备

- **WHEN** CPL3 code triggers syscall, exception, or external interrupt handling
- **THEN** the CPU MUST have a valid kernel stack return mechanism such as a configured TSS/RSP0 or equivalent architecture-supported state
- **AND** this setup MUST be initialized before entering the first user program

### Requirement: 用户态 syscall write/exit 闭环

BigOS SHALL allow the first user program to invoke a minimal syscall set from ring3 through the established syscall ABI, including a bounded write capability and a process exit capability.

#### Scenario: 用户 write 输出确定性 marker

- **WHEN** the first user program invokes the write syscall with a user buffer containing the expected smoke payload
- **THEN** the kernel MUST validate or safely copy the bounded user buffer before reading it
- **AND** the kernel MUST emit a deterministic `BIGOS_USER_` or `BIGOS_SYSCALL_` marker to the configured diagnostic sink

#### Scenario: 非法用户 buffer 不破坏内核

- **WHEN** the write syscall receives an unmapped, kernel-space, non-user, or overlong user buffer
- **THEN** BigOS MUST return a deterministic error or terminate the current user process
- **AND** BigOS MUST NOT treat the failure as a successful write or read arbitrary kernel memory

#### Scenario: 用户 exit 终止进程

- **WHEN** the first user program invokes the exit syscall with an exit code
- **THEN** BigOS MUST record the exit code and mark the process terminated
- **AND** the syscall MUST NOT return to the terminated user instruction stream

### Requirement: 用户态 fault 受控处理

BigOS SHALL distinguish user-mode faults from kernel faults for the first user program and handle user faults with deterministic diagnostic or termination behavior without implementing demand paging or signal delivery.

#### Scenario: 用户态页错误被识别

- **WHEN** a page fault occurs while the interrupted context is CPL3
- **THEN** BigOS MUST recognize it as a user-mode fault using the saved code segment or equivalent privilege information
- **AND** BigOS MUST emit a deterministic user fault marker or terminate the current user process

#### Scenario: 内核页错误仍保持诊断优先

- **WHEN** a page fault occurs while the interrupted context is kernel mode
- **THEN** the existing diagnostic-only kernel page fault behavior MUST remain intact
- **AND** BigOS MUST NOT silently recover kernel faults as if they were user faults

#### Scenario: 不实现 demand paging

- **WHEN** a user page fault is caused by an unmapped user page
- **THEN** BigOS MUST NOT allocate pages on demand or resume the faulting instruction as a successful demand-paging recovery
- **AND** the fault path MUST record the failure or terminate the process

### Requirement: 首个用户程序验证可复现

BigOS SHALL provide reproducible source-level, build-level, and optional emulator validation for the first user program path.

#### Scenario: 源码级检查覆盖用户态不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover embedded user image wiring, user page attributes, controlled CR3 activation, ring3 entry frame construction, syscall gate DPL, user pointer validation, exit handling, and CPL3 fault detection
- **AND** the checks MUST confirm boot fixed addresses, higher-half base, direct map, `KVMEM_BASE`, and self-mapping constants are not moved

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` or cross-toolchain build, relevant `uv run pytest` source checks, and `openspec validate load-first-user-program --strict`
- **AND** if Bochs runtime smoke cannot observe `BIGOS_USER_` markers due to emulator, ROM, serial/VGA oracle, image lock, or interactive limitations, validation MUST record the unavailable dependency and remaining bootability risk
