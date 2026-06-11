## ADDED Requirements

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
