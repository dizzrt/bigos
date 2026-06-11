## ADDED Requirements

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
