## ADDED Requirements

### Requirement: Stage 40 crt0 保持静态 C 程序入口边界

BigOS 用户态 crt0 SHALL remain the freestanding entry path for statically linked C user programs in the Stage 40 C library foundation. crt0 MUST preserve the documented `_start` behavior, initial stack interpretation, `main(argc, argv, envp)` call shape, `environ` handoff, and `SYS_EXIT` termination behavior. crt0 MUST NOT introduce a dynamic loader, shared library initialization, hosted runtime startup, thread runtime, C++ global constructor execution, exception/RTTI dependency, or host libc dependency.

#### Scenario: 静态程序入口不依赖动态 loader

- **WHEN** 一个 Stage 40 静态 C 用户程序经现有用户 ELF 装载路径启动
- **THEN** crt0 MUST 按当前用户栈契约读取 `argc`、NULL 结尾 `argv` 和 NULL 结尾 `envp`
- **AND** crt0 MUST 直接调用用户 `main(argc, argv, envp)` 而不要求动态 loader、共享库或 hosted runtime 预初始化

#### Scenario: main 返回仍经退出 syscall

- **WHEN** Stage 40 静态 C 用户程序的 `main` 返回一个整数状态码
- **THEN** crt0 MUST 通过现有稳定 syscall ABI 终止进程
- **AND** crt0 MUST NOT 返回到未定义调用者或依赖动态运行时析构路径

### Requirement: crt0 与 libc 基础子集协作但不扩大 ABI

BigOS 用户态 crt0 SHALL cooperate with the Stage 40 libc foundation only through documented user entry, environment, calling convention, and exit contracts. crt0 MAY publish `environ` or equivalent libc-visible state required for read-only environment access, but it MUST NOT depend on libc internals that require dynamic initialization, heap allocation, file I/O, scheduler internals, VFS internals, interrupt frame layout, or kernel-private process structures.

#### Scenario: environ 交接保持有界

- **WHEN** crt0 接收到内核布置的 `envp`
- **THEN** crt0 MAY make the same environment vector visible to libc read-only environment helpers
- **AND** 该交接 MUST 不要求复制环境数据库、动态分配、线程局部状态或动态链接器参与

#### Scenario: crt0 不消费内核私有结构

- **WHEN** 审查 Stage 40 crt0 与 libc 启动协作代码
- **THEN** crt0 MUST 只依赖文档化的用户 ELF 入口、初始用户栈、C 调用约定和退出 syscall ABI
- **AND** MUST NOT include or infer kernel-private process、VMA、scheduler、VFS、interrupt frame 或 backend implementation details
