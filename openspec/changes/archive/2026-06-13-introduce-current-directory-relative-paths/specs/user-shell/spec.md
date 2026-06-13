## ADDED Requirements

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
