## Purpose

定义 BigOS 最小交互式 `/bin/sh` 能力：提供有界读-解析-执行循环、内建命令、基于 `fork`/`execve`/`wait` 的外部命令执行、PATH 查找，以及单级管道和基本重定向。
## Requirements
### Requirement: 交互式读-解析-执行循环

BigOS SHALL 提供一个最小交互式 shell `/bin/sh`（C 语言，链接用户 crt0 与用户 libc）。shell MUST 从标准输入读取一行、按空白把该行解析为命令名与参数（argv），并执行命令，随后回到读行状态。行长、argv 个数与管道段数 MUST 有界；超出上界时 shell MUST 确定性报错并继续循环，而不是崩溃。

#### Scenario: 读行并解析为 argv

- **WHEN** 用户在 shell 提示符下输入一行以换行结束的命令文本
- **THEN** shell MUST 读取该行并按空白把它解析为命令名与参数数组
- **AND** 解析结果 MUST 以 NULL 结尾的 argv 形式提供给执行步骤

#### Scenario: 超出有界上限时确定性报错

- **WHEN** 输入行长度、argv 个数或管道段数超过 shell 的有界上限
- **THEN** shell MUST 输出确定性错误信息
- **AND** shell MUST 回到读行循环而不是崩溃或进入未定义状态

### Requirement: 内建命令

BigOS shell SHALL 支持一组最小内建命令，至少包含 `exit`（以给定退出码或 0 终止 shell）与 `echo`（把参数回显到 stdout）。内建命令 MUST 在 shell 进程内直接处理，而不经 `fork`/`execve`。

#### Scenario: exit 内建终止 shell

- **WHEN** 用户输入 `exit`（可带退出码）
- **THEN** shell MUST 以给定退出码（缺省为 0）调用 `exit` 终止自身

#### Scenario: echo 内建回显参数

- **WHEN** 用户输入 `echo` 加若干参数
- **THEN** shell MUST 把这些参数以空格分隔回显到 stdout 并换行

### Requirement: Shell cwd 内建命令

BigOS shell SHALL consume the current-directory capability in the shell process. The shell MUST provide a bounded `cd` builtin that calls the libc/kernel `chdir` path, supports BigOS path resolution including POSIX-style `.`/`..` components, reports deterministic errors, and returns to the read-parse-execute loop. `cd` MUST run in the shell process rather than a forked child, and MUST NOT imply full POSIX shell expansion, globbing, job control, sessions, terminal process groups, or complete `realpath` behavior.

#### Scenario: cd 改变 shell cwd

- **WHEN** 用户在 shell 中输入 `cd` 指向一个存在目录
- **THEN** shell MUST call the cwd-changing path in the shell process
- **AND** subsequent relative command paths and redirection paths MUST resolve from the new cwd

#### Scenario: cd 失败后 shell 保持可用

- **WHEN** 用户在 shell 中输入 `cd` 指向缺失对象、普通文件、过长路径或不支持路径形式
- **THEN** shell MUST report a deterministic error through stdout or stderr
- **AND** shell MUST keep the previous cwd and return to the next prompt/read loop

#### Scenario: cd dot-dot 返回父目录

- **WHEN** 用户在 cwd `/rw/work/sub` 的 shell 中输入 `cd ..`
- **THEN** shell MUST update its cwd to `/rw/work` through the kernel `chdir` contract
- **AND** subsequent relative paths MUST resolve from `/rw/work`

#### Scenario: cd 不经 fork 执行

- **WHEN** shell recognizes `cd` as a builtin
- **THEN** it MUST execute the cwd update in the current shell process
- **AND** it MUST NOT run `cd` through `fork`/`execve` where the cwd change would be lost when the child exits

### Requirement: Shell 相对路径消费

BigOS shell SHALL let supported command execution, explicit path commands, input/output redirection, and small user tools consume relative paths through the kernel cwd contract. Shell parsing MUST remain bounded and MUST NOT add full POSIX shell grammar, globbing, variable expansion, tilde expansion, or scripting semantics as part of cwd support.

#### Scenario: 含斜杠相对命令按 cwd 执行

- **WHEN** 用户在 shell 中输入包含 `/` 的相对命令路径
- **THEN** shell MUST pass that path to `execve` without converting it to a host-style absolute path
- **AND** kernel path resolution MUST determine whether the cwd-resolved target exists and is executable under the BigOS user ELF subset

#### Scenario: 重定向路径按 cwd 解析

- **WHEN** 用户在 shell 中使用 supported redirection with a relative file path
- **THEN** shell MUST call libc open wrapper with that relative path
- **AND** fd/VFS MUST resolve it from shell cwd while preserving redirection failure isolation for shell fd state

#### Scenario: shell 范围保持有界

- **WHEN** cwd behavior is documented, prompted, or validated through shell
- **THEN** it MUST be described as bounded BigOS shell path handling with POSIX-style `.`/`..` component support
- **AND** MUST NOT imply full POSIX shell language, globbing, quoting, job control, terminal process groups, symlink traversal, or complete `realpath`

### Requirement: 小型 pwd 用户工具

BigOS userland SHALL provide a small static `/bin/pwd` user program that reports the current process cwd through the libc `getcwd` wrapper. The tool MUST print a deterministic current-directory string to stdout on success, report deterministic errno-based errors on failure, and remain within the minimal freestanding userland model. It MUST NOT require shell builtin support, hosted stdio, dynamic linking, locale, symlink-aware realpath behavior, or complete POSIX utility semantics.

#### Scenario: pwd 输出当前目录

- **WHEN** 用户在 shell 中执行 `/bin/pwd` 或通过 PATH 执行 `pwd`
- **THEN** the program MUST call libc `getcwd` and write the current cwd to stdout
- **AND** the output MUST be deterministic enough for runtime validation and manual inspection

#### Scenario: pwd 在 exec 后观察继承 cwd

- **WHEN** shell changes cwd with `cd` and then executes `/bin/pwd`
- **THEN** `/bin/pwd` MUST observe the cwd preserved across `fork`/`execve`
- **AND** the shell MUST continue its bounded read-parse-execute loop after the tool exits

#### Scenario: pwd 错误路径可报告

- **WHEN** `getcwd` fails because of buffer sizing, user-memory validation, or another deterministic kernel error
- **THEN** `/bin/pwd` MUST report an errno-based failure through stderr or stdout and exit nonzero
- **AND** MUST NOT require hosted libc error formatting or complete POSIX utility behavior

### Requirement: 经 fork+execve+wait 运行外部命令

BigOS shell SHALL 对非内建命令以 `fork` 创建子进程、在子进程中以解析出的 argv `execve` 目标程序、在父进程中 `wait` 子进程结束并取得其退出状态。目标程序路径按命令查找规则确定（见"命令查找与 PATH"需求）。当 `execve` 最终失败（如目标不存在）时，子进程 MUST 确定性报错并以非零码退出，父 shell MUST 不被破坏并继续循环。

#### Scenario: 运行存在的外部命令

- **WHEN** 用户输入一个能经命令查找定位到存在可执行文件的命令
- **THEN** shell MUST `fork` 子进程、在子进程 `execve` 该程序、在父进程 `wait` 其结束
- **AND** shell MUST 在子进程结束后回到读行循环

#### Scenario: execve 失败确定性处理

- **WHEN** 命令查找耗尽所有候选仍无法定位或加载目标导致 `execve` 失败
- **THEN** 子进程 MUST 输出确定性错误信息并以非零退出码退出
- **AND** 父 shell MUST 继续正常循环而不崩溃

### Requirement: 命令查找与 PATH

BigOS shell SHALL 在用户态实现命令查找。当命令名包含 `/`（绝对或相对路径）时，shell MUST 直接以该路径 `execve`。当命令名不含 `/` 时，shell MUST 按 `PATH` 环境变量（经 `envp` 传入；缺省回退到固定默认目录 `/bin`）从左到右逐个目录拼接命令名并尝试 `execve`，命中即运行，全部候选耗尽则报 "command not found"。候选目录数与拼接后的路径长度 MUST 有界（复用 `SYS_PATH_MAX_LEN` 上界），超界候选 MUST 被跳过或确定性报错而非越界。

#### Scenario: 含斜杠的命令直接按路径执行

- **WHEN** 用户输入的命令名包含 `/`（如 `/bin/echo` 或 `./prog`）
- **THEN** shell MUST 直接以该路径 `execve`，而不查 `PATH`

#### Scenario: 不含斜杠的命令按 PATH 查找

- **WHEN** 用户输入的命令名不含 `/`（如 `echo`）
- **THEN** shell MUST 按 `PATH`（缺省 `/bin`）逐目录拼接并尝试 `execve`
- **AND** 命中第一个可执行的候选即运行

#### Scenario: PATH 查找全部失败

- **WHEN** 所有 `PATH` 候选目录下均无法定位或加载该命令
- **THEN** shell MUST 输出 "command not found" 类确定性错误
- **AND** shell MUST 回到读行循环而不崩溃

### Requirement: 单级管道与基本重定向

BigOS shell SHALL 支持单级管道 `cmd1 | cmd2`（经 `SYS_PIPE` 连接、用 `SYS_DUP2` 把上游 stdout 接到下游 stdin）与基本文件重定向 `> file`（stdout 写入文件，经 `open` + `SYS_DUP2`）与 `< file`（stdin 读自文件）。重定向打开文件 MUST 经现有可写/只读 FS 与权限路径；失败 MUST 确定性报错。

#### Scenario: 单级管道连接两个命令

- **WHEN** 用户输入 `cmd1 | cmd2`
- **THEN** shell MUST 创建一个管道，把 `cmd1` 的 stdout 经 `dup2` 接到管道写端、把 `cmd2` 的 stdin 经 `dup2` 接到管道读端
- **AND** shell MUST 等待两个子进程结束后回到读行循环

#### Scenario: 输出重定向到文件

- **WHEN** 用户输入 `cmd > file`
- **THEN** shell MUST 以可写/创建方式 `open` 该文件并把命令的 stdout 经 `dup2` 指向该文件
- **AND** 当 `open` 因权限或只读后端失败时 shell MUST 确定性报错且不执行该命令

### Requirement: Shell prompt is visible on the default interactive console
BigOS shell SHALL display a deterministic bounded prompt before waiting for an interactive command line when stdin/stdout are connected to the default TTY/console path.

#### Scenario: Prompt appears before input
- **WHEN** normal boot starts resident init and enters `/bin/sh` with standard descriptors connected to the default interactive console
- **THEN** shell MUST write a visible prompt to stdout before blocking for the next command line
- **AND** the prompt MUST be bounded and deterministic enough for manual validation

#### Scenario: Prompt does not pollute non-interactive execution
- **WHEN** shell stdin or stdout is redirected away from the default interactive TTY/console path
- **THEN** shell MUST avoid treating that path as an interactive prompt session
- **AND** command output and redirection semantics MUST remain bounded and deterministic

### Requirement: Shell command interaction is visible through console stdout and stderr
BigOS shell SHALL make command execution feedback visible on the default text console by routing built-in command output, external command stdout/stderr, and deterministic shell error messages through the existing userland fd/syscall path.

#### Scenario: Built-in command output is visible
- **WHEN** 用户在默认控制台 shell 中输入内建命令 `echo hello`
- **THEN** shell MUST 将内建命令输出写入 stdout
- **AND** 该输出 MUST 能通过默认文本控制台被用户看到

#### Scenario: External command output is visible
- **WHEN** 用户在默认控制台 shell 中运行存在的简单外部命令
- **THEN** shell MUST 通过现有 `fork`/`execve`/`wait` 路径执行该命令
- **AND** 子进程 stdout/stderr 的有界文本输出 MUST 能通过默认文本控制台被用户看到

#### Scenario: Shell error is visible and recoverable
- **WHEN** 用户输入不存在的命令、超出有界输入限制或触发可恢复解析错误
- **THEN** shell MUST 向 stderr 或 stdout 输出确定性错误信息
- **AND** shell MUST 回到下一次 prompt/read 循环而不崩溃

### Requirement: Shell 执行 基线 C 程序并保留可观察结果

BigOS shell SHALL 能执行 基线小型静态 C 程序，并保留其用户可观察行为：参数传递、stdout/stderr 输出、退出状态和确定性错误报告。该需求 MUST 不扩展为完整 POSIX shell、作业控制、terminal process group 或复杂脚本语义。

#### Scenario: Shell 传递 argv 给外部 C 程序

- **WHEN** 用户或验证输入通过 shell 启动一个 基线 C 程序并携带参数
- **THEN** shell MUST 以 NULL 结尾 `argv` 调用现有外部命令执行路径
- **AND** 被执行程序 MUST 能观察到对应 `argc` 和参数字符串

#### Scenario: Shell 展示程序输出和错误

- **WHEN** 基线 C 程序向 stdout 或 stderr 写入文本
- **THEN** shell 的交互或验证路径 MUST 让这些输出经当前 fd/console/serial 可观察
- **AND** shell MUST NOT 把外部程序的普通错误报告误处理为 shell 自身崩溃

#### Scenario: Shell 在程序结束后继续运行

- **WHEN** 基线 C 程序正常退出或以非零状态退出
- **THEN** shell MUST 回到有界读-解析-执行循环
- **AND** shell MUST 保留外部程序退出状态的可观察性，至少用于验证路径判断成功或失败

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

### Requirement: Shell consumes bounded terminal control input

BigOS shell SHALL consume the default terminal's bounded control-character semantics through stdin and its existing line-input/read-parse-execute loop. The shell MUST handle line end, backspace/delete-like editing feedback, EOF-like input, interrupt-like input, and unsupported control bytes deterministically without requiring termios, sessions, job control, terminal process groups, full POSIX shell language, or dynamic linking.

#### Scenario: EOF-like input has a deterministic shell result

- **WHEN** `/bin/sh` reads EOF-like input from the default terminal while waiting for a command line
- **THEN** shell MUST produce a deterministic bounded result such as ending the current input loop, exiting with a documented status, or reporting a documented no-op
- **AND** the result MUST NOT require POSIX canonical-mode completeness, shell variables, scripts, or terminal process groups

#### Scenario: Interrupt-like input has a deterministic shell result

- **WHEN** `/bin/sh` receives interrupt-like terminal input while reading a command line or waiting for a supported foreground command
- **THEN** shell MUST handle it as a bounded BigOS terminal result such as cancelling the current line, reporting a deterministic message, or documenting no-op behavior
- **AND** the result MUST NOT imply POSIX job control, signal delivery to terminal process groups, sessions, or full foreground/background process semantics

#### Scenario: Unsupported controls do not corrupt shell state

- **WHEN** the default terminal delivers an unsupported control byte or unsupported terminal event to `/bin/sh`
- **THEN** shell MUST ignore it or report deterministic bounded feedback
- **AND** shell MUST return to a valid prompt/read state without corrupting argv parsing, redirection state, pipe state, cwd, or bounded status

### Requirement: Shell terminal feedback remains visible through stdout and stderr

BigOS shell SHALL make prompt text, line-input feedback, control-character feedback, and deterministic shell errors visible through stdout or stderr when connected to the default terminal. The shell MUST NOT directly access hardware output paths or early diagnostic-only APIs for ordinary interactive behavior.

#### Scenario: Prompt and feedback use ordinary output

- **WHEN** shell stdin/stdout/stderr are connected to the default terminal
- **THEN** shell MUST write prompt text and supported line-input feedback through stdout or stderr
- **AND** the resulting output MUST remain compatible with existing fd inheritance, redirection, pipe, and console output boundaries

#### Scenario: Terminal feedback does not replace command output

- **WHEN** shell executes builtins or external commands after terminal input processing
- **THEN** command stdout/stderr MUST remain visible through the ordinary fd/syscall path
- **AND** prompt, echo, or control-character feedback MUST NOT be confused with child process failure, shell crash, or smoke marker output

### Requirement: Shell fd isolation after failure
The BigOS shell SHALL preserve its parent-loop standard input, output, and error descriptors after failed redirection, failed pipe setup, failed fork, failed exec, unsupported syntax, or child command failure.

#### Scenario: Failed output redirection does not break shell stdout
- **WHEN** a command with output redirection fails before the child command runs
- **THEN** the shell reports the error, returns to the prompt or next command, and later output still appears on the original stdout

#### Scenario: Failed input redirection does not break shell stdin
- **WHEN** a command with input redirection fails because the input path cannot be opened
- **THEN** the shell reports the error, keeps the original stdin usable, and can read the next command

### Requirement: Shell single-pipe status and close behavior
The BigOS shell SHALL keep single-stage pipe support bounded and deterministic, closing unused pipe endpoints in all participating processes and reporting the right-side command status as the pipe command status.

#### Scenario: Pipe writer EOF reaches reader
- **WHEN** the left command exits and all write ends are closed
- **THEN** the right command observes EOF after consuming the pipe contents

#### Scenario: Pipe status follows right command
- **WHEN** both sides of a single-stage pipe complete
- **THEN** the shell records the bounded status of the right-side command as the pipeline status

### Requirement: Shell Stage 39 diagnostic consistency
The BigOS shell SHALL produce deterministic diagnostics and bounded statuses for command-not-found, exec failure, unsupported syntax, parse errors, and external command non-zero exits.

#### Scenario: Command not found status
- **WHEN** a command without a slash cannot be found through the bounded PATH lookup
- **THEN** the shell reports a command-not-found diagnostic and records status 127

#### Scenario: Unsupported syntax status
- **WHEN** a command line contains syntax outside the supported bounded shell grammar
- **THEN** the shell reports unsupported syntax and records status 2 without partially executing the command

