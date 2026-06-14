## ADDED Requirements

### Requirement: Shell 错误恢复与状态传播硬化

BigOS shell SHALL 对解析失败、unsupported syntax、PATH 查找失败、重定向 setup 失败、pipe setup 失败、`fork` 失败、`execve` 失败和子进程非零退出提供确定性错误与 bounded status 传播。shell MUST 在可恢复失败后回到 read-parse-execute 循环，MUST 保留最近一次内建命令、外部命令或支持的组合命令的有界结果状态供验证路径和 shell 内部决策使用，并 MUST NOT 将该状态承诺为完整 POSIX `$?`、变量展开或脚本变量语义。

#### Scenario: 命令缺失后 shell 保持可用

- **WHEN** 用户输入一个 PATH 查找无法定位的命令名
- **THEN** shell MUST 输出确定性 command-not-found 类错误
- **AND** shell MUST 记录非零 bounded status 并回到下一次 prompt/read 循环

#### Scenario: 不支持语法被拒绝

- **WHEN** 用户输入超出 BigOS bounded shell grammar 的语法形式
- **THEN** shell MUST 输出确定性 unsupported-syntax 或 parse-error 类错误
- **AND** shell MUST NOT 尝试以未定义 argv、pipe 或 redirection 状态执行命令

#### Scenario: 内建与外部命令更新最近状态

- **WHEN** shell 执行内建命令、成功外部命令、失败外部命令或 exec 失败路径
- **THEN** shell MUST 将该命令结果折叠为 bounded status
- **AND** 后续验证路径 MUST 能通过 shell 行为、输出或专用验证程序观察成功与失败差异

#### Scenario: 最近状态不要求 POSIX 变量暴露

- **WHEN** shell 维护最近一次 bounded status
- **THEN** shell MUST NOT require POSIX `$?`, variable expansion, or scripting semantics to expose that status
- **AND** if an interactive status observer is added, it MUST be a BigOS-specific bounded builtin that only reports the stored status

### Requirement: Shell redirection 与 pipe fd 隔离

BigOS shell SHALL 在支持的 `<`、`>` 和单级 pipe 组合中保护父 shell 的标准 fd 和无关 fd。setup 成功后目标子进程 MUST 观察到重定向或 pipe fd 映射；setup 失败时 shell MUST 关闭已创建但未交付的 fd，恢复父 shell stdin/stdout/stderr，并以非零 bounded status 继续交互循环。

#### Scenario: 重定向打开失败不污染父 shell

- **WHEN** 用户运行带输出或输入重定向的命令，且目标路径因只读后端、缺失对象、权限、容量或路径限制失败
- **THEN** shell MUST 输出确定性 redirection error
- **AND** shell MUST NOT 执行目标命令
- **AND** 后续普通命令 MUST 仍通过父 shell 原有 stdin/stdout/stderr 运行

#### Scenario: pipe setup 失败清理中间 fd

- **WHEN** shell 为单级 pipe 创建 pipe 端点、fork 子进程或 dup fd 的过程中失败
- **THEN** shell MUST 关闭已经创建但不再使用的 pipe 或文件 fd
- **AND** shell MUST 报告确定性错误并保留父 shell 后续命令可用性

#### Scenario: 支持的 pipe 组合采用末端命令状态

- **WHEN** 用户运行支持的 `cmd1 | cmd2` 组合且两端均启动
- **THEN** shell MUST 等待两个子进程完成
- **AND** shell MUST record the bounded pipeline status from the terminal command `cmd2`
- **AND** upstream command failure MUST remain observable through deterministic stdout/stderr, child status in validation paths, or another bounded diagnostic path where available

### Requirement: Shell 路径与小工具组合可用性

BigOS shell SHALL 让支持的 packaged path tools、简单静态 C 程序和内建命令在 PATH 查找、cwd-relative path、basic redirection 和 single pipe 组合下保持可观察 stdout/stderr、errno-based failure 和 bounded exit status。该能力 MUST 保持在 BigOS bounded shell 子集内，MUST NOT 引入 globbing、quoting expansion、variables、command substitution、多级 pipeline、job control、sessions 或 terminal process groups。

#### Scenario: cwd-relative 工具输出可重定向

- **WHEN** shell cwd 位于支持目录且用户运行路径工具并将 stdout 重定向到 cwd-relative writable path
- **THEN** shell MUST 通过现有 open/dup2/fork/execve 路径设置输出目标
- **AND** 后续支持的文件内容或 metadata 工具 MUST 能观察该输出文件

#### Scenario: 路径工具可通过单级 pipe 组合

- **WHEN** 用户把一个支持的 path tool stdout 连接到另一个支持的简单命令 stdin
- **THEN** shell MUST 通过现有 pipe/fd 语义传递数据
- **AND** 组合输出、错误或状态 MUST 保持可观察且 bounded

#### Scenario: 组合失败保持 shell 范围有界

- **WHEN** path tool、简单 C 程序、redirection 或 pipe 组合中的任一受支持操作失败
- **THEN** shell MUST 报告确定性错误或传播子进程非零状态
- **AND** shell MUST NOT 暗示完整 POSIX shell language、完整 POSIX utility suite 或完整 terminal 行为
