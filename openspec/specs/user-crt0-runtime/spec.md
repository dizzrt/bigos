## Purpose

定义 BigOS 用户态 C 运行时启动代码能力：提供 freestanding 的 `_start`/crt0 入口，遵守内核用户 ELF 装载器与初始栈契约，把 `argc`/`argv`/`envp` 传递给 `main`，并在 `main` 返回后通过 `SYS_EXIT` 终止进程。

## Requirements

### Requirement: 用户态 C 运行时启动代码 (crt0)

BigOS SHALL 提供一段用户态 C 运行时启动代码（crt0），作为用户 ELF 程序的真实入口点 `_start`。crt0 MUST 按内核 ELF 装载器与 `copy_exec_args_to_stack` 约定的初始用户栈布局，读取 `argc`、`argv`（以 NULL 结尾的指针数组）与 `envp`，按 System V x86_64 约定对齐栈并以 `main(argc, argv, envp)` 形式调用用户 `main`，在 `main` 返回后以其返回值经 `SYS_EXIT` 退出进程。crt0 MUST 是 freestanding 的：不依赖任何宿主运行时、不假定 libc 已初始化、不执行 C++ 全局构造，且绝不返回到未定义地址。

#### Scenario: crt0 传递 argc/argv 并调用 main

- **WHEN** 内核经 ELF 装载器加载一个链接了 crt0 的用户程序并进入 ring3
- **THEN** `_start` MUST 从初始用户栈读取与内核布置一致的 `argc` 与 `argv`
- **AND** `_start` MUST 按对齐约定调用 `main(argc, argv, envp)`
- **AND** `argv[argc]` MUST 为 NULL 终止指针

#### Scenario: main 返回后经 SYS_EXIT 退出

- **WHEN** 用户 `main` 返回一个整型退出码
- **THEN** crt0 MUST 以该退出码调用 `SYS_EXIT`
- **AND** crt0 MUST NOT 在 `main` 返回后继续执行到未定义地址或返回到 `_start` 调用者

#### Scenario: crt0 为 freestanding

- **WHEN** 审查 crt0 源码与其链接产物
- **THEN** crt0 MUST NOT 依赖宿主 C/C++ 运行时、动态链接器或 C++ 全局构造
- **AND** crt0 MUST 仅通过用户 libc 或直接的 `int 0x80` 与内核交互

### Requirement: crt0 与用户 ELF 装载契约一致

crt0 SHALL 与内核现有用户 ELF64 `ET_EXEC` 装载约定（入口地址、初始栈指针、`argc`/`argv` 布局、`-nostdlib -static` 链接、bounded 体积上限）保持一致，使链接了 crt0 的用户程序经现有内核装载路径加载即可正确运行，无需修改内核 ELF 装载器的 bounded 限制或 ABI。

#### Scenario: crt0 程序经现有装载器运行

- **WHEN** 一个链接了 crt0 的用户程序被打包进磁盘镜像并经内核 VFS/ELF 装载路径加载
- **THEN** 内核 MUST 无需放宽既有 ELF 装载器 bounded 限制即可加载并进入该程序
- **AND** 初始用户栈布局 MUST 与 crt0 读取 `argc`/`argv`/`envp` 的假设一致

### Requirement: 简单 C 程序入口契约稳定

BigOS 用户态 crt0 SHALL 为简单静态 C 程序提供稳定入口契约：从现有用户栈布局读取 `argc`、`argv`、`envp`，以 `main(argc, argv, envp)` 形式调用程序入口，并在 `main` 返回后以返回值作为进程退出码退出。该契约 MUST 不依赖宿主 runtime、动态链接、共享库或 C++ 全局构造。

#### Scenario: main 接收参数和环境

- **WHEN** 一个 基线 C 程序由 `execve` 以有界 `argv` 和 `envp` 启动
- **THEN** crt0 MUST 将相同的 `argc`、NULL 结尾 `argv` 和 NULL 结尾 `envp` 传给 `main`
- **AND** 程序 MUST 能在用户态读取这些字符串而不需要了解内核栈布局细节

#### Scenario: main 返回值成为退出码

- **WHEN** 一个 基线 C 程序的 `main` 返回一个有界整数状态码
- **THEN** crt0 MUST 经现有退出 syscall 结束进程
- **AND** 父进程或 shell MUST 能观察到对应退出状态，而不是让程序返回到未定义地址

#### Scenario: 入口契约不引入 hosted runtime

- **WHEN** 构建 基线 C 程序
- **THEN** crt0 MUST NOT 要求宿主 libc、动态加载器、共享库、线程 runtime、C++ exception/RTTI 或全局构造支持

### Requirement: crt0 入口只依赖稳定用户入口契约
BigOS userland crt0 SHALL depend only on the documented user ELF entry contract, initial user stack layout, C calling convention needed to call `main`, and the stable syscall ABI needed to terminate the process. crt0 MUST NOT depend on kernel-private process structs, VMA metadata, interrupt frame layout, scheduler internals, or undocumented ring3 entry implementation details.

#### Scenario: crt0 读取文档化初始栈
- **WHEN** a statically linked user program starts at `_start`
- **THEN** crt0 MUST read `argc`, NULL-terminated `argv`, and `envp` according to the documented initial user stack contract
- **AND** it MUST NOT rely on hidden kernel stack contents or backend-private handoff state

#### Scenario: crt0 退出经稳定 syscall ABI
- **WHEN** `main` returns from a user program
- **THEN** crt0 MUST terminate the process through the documented exit syscall wrapper or direct stable syscall ABI
- **AND** it MUST NOT return to an undefined caller or use a kernel-private termination path

### Requirement: crt0 边界整理保持 freestanding runtime
BigOS userland crt0 SHALL remain freestanding and static-link friendly while syscall/user ABI boundaries are hardened. The cleanup MUST NOT introduce hosted runtime initialization, dynamic linking, C++ global constructors, thread runtime, or host libc dependencies.

#### Scenario: crt0 构建保持 freestanding
- **WHEN** user programs are linked with crt0 during the default build
- **THEN** crt0 MUST remain compatible with `-nostdlib -static` user program linking
- **AND** it MUST NOT require host libc, a dynamic loader, shared libraries, exceptions, RTTI, or thread runtime support

#### Scenario: crt0 不扩大用户态 ABI
- **WHEN** crt0 is updated for ABI boundary cleanup
- **THEN** it MUST preserve the existing entry, stack, and exit behavior for supported static C programs
- **AND** it MUST NOT add new user-visible ABI requirements unless corresponding specs and docs are updated

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
