## ADDED Requirements

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
