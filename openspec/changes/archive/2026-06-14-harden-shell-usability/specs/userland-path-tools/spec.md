## ADDED Requirements

### Requirement: 路径工具在 shell 组合中保持确定性行为

BigOS userland path tools SHALL remain deterministic when consumed by the bounded shell through PATH lookup, cwd-relative paths, stdout redirection, stdin redirection where applicable, and single-pipe composition. Each tool MUST report supported failures through errno-based output or deterministic tool errors, MUST return bounded nonzero status on failure, and MUST NOT require complete POSIX utility options, recursive traversal, globbing, scripting, locale-aware formatting, dynamic linking, hosted libc, or complete POSIX shell behavior.

#### Scenario: PATH 查找运行工具并传播状态

- **WHEN** 用户在 shell 中通过 PATH 运行一个 packaged path tool
- **THEN** shell MUST launch it through the existing external command path
- **AND** the tool MUST return a bounded status that lets shell and validation distinguish success from deterministic failure

#### Scenario: 工具错误不破坏 shell 循环

- **WHEN** a path tool fails because of missing path, unsupported object type, read-only backend mutation, path length, permission, or capacity boundary
- **THEN** the tool MUST report a deterministic errno-based or bounded tool error and exit nonzero
- **AND** shell MUST remain able to run a subsequent command

#### Scenario: 工具组合不扩大工具集承诺

- **WHEN** documentation, help text, specs, or validation describe path tools used with pipe or redirection
- **THEN** they MUST describe the behavior as BigOS bounded path-tool composition
- **AND** they MUST NOT claim complete POSIX `ls`, `cat`, `stat`, `mkdir`, `rm`, `mv`, shell language, globbing, recursive traversal, symlink traversal, or persistent filesystem behavior

### Requirement: 路径工具输出可经 redirection 与 pipe 观察

BigOS path tools SHALL provide stdout/stderr behavior that composes with the bounded shell's existing fd, redirection, and pipe contracts. Tool output redirected to writable runtime paths MUST be observable by later supported path tools when the backend supports it; pipe output MUST be consumable by a supported downstream command within bounded pipe limits.

#### Scenario: 重定向保存路径工具输出

- **WHEN** a user redirects a supported path tool's stdout to a cwd-relative file under a writable runtime directory
- **THEN** the shell MUST route stdout through existing open/dup2 semantics
- **AND** a later supported file-content or metadata tool MUST be able to observe the redirected file if the write succeeded

#### Scenario: pipe 传递路径工具输出

- **WHEN** a user connects a supported path tool's stdout to another supported command through a single pipe
- **THEN** data written by the upstream tool MUST be available to the downstream command according to bounded pipe semantics
- **AND** unsupported syntax, upstream failure, downstream failure, or pipe capacity limits MUST remain deterministic and recoverable
