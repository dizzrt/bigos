## ADDED Requirements

### Requirement: cwd 和相对路径纳入有界 POSIX-like 子集
BigOS SHALL include per-process current directory and relative path handling in the bounded POSIX-like process and I/O subset. Simple programs MAY rely on cwd inheritance across `fork`, preservation across `execve`, `chdir`/`getcwd` wrappers, POSIX-style `.`/`..` component handling in the supported directory tree, and relative path resolution for supported path-taking operations. This subset MUST remain explicitly bounded and MUST NOT claim sessions, terminal process groups, job control, mount namespaces, `chroot`, symlink traversal, complete POSIX pathname canonicalization, dynamic linking, SMP, async I/O, or complete POSIX libc.

#### Scenario: 简单程序依赖 cwd 子集
- **WHEN** a simple statically linked C program uses only documented cwd wrappers and path-taking file wrappers
- **THEN** it MUST be able to rely on kernel cwd resolution, libc errno translation, and fd/VFS behavior within the bounded subset
- **AND** it MUST NOT require hosted runtime, dynamic loader, full POSIX libc, namespace support, or symlink support

#### Scenario: fork 和 exec 组合保持 cwd
- **WHEN** shell or a parent process changes cwd, forks a child, and the child execs a packaged static user program
- **THEN** the child program MUST observe cwd according to the documented inheritance and exec preservation rules
- **AND** the behavior MUST remain independent of process groups, sessions, or terminal control

#### Scenario: pwd 展示有界 cwd 子集
- **WHEN** a user executes the packaged `/bin/pwd` tool after changing cwd
- **THEN** the tool MUST display the current directory through the documented libc/kernel cwd contract
- **AND** the output MUST NOT imply symlink-aware `realpath`, mount namespace, or complete POSIX utility behavior

#### Scenario: 文档不扩大兼容承诺
- **WHEN** BigOS documentation or OpenSpec artifacts describe relative path support
- **THEN** they MUST describe it as a bounded POSIX-like subset
- **AND** MUST NOT imply full POSIX filesystem, permissions, namespace, symlink, shell, or terminal semantics

### Requirement: cwd 行为验证纳入组合路径
BigOS SHALL extend behavior-oriented validation for the bounded POSIX-like process and I/O subset to cover current-directory behavior. Validation MUST include process lifecycle composition, fd/path operation composition, shell consumption, user-visible error reporting, and environment-dependent skip records.

#### Scenario: validation observes shell and process cwd
- **WHEN** process/I/O subset runtime validation runs in a configured emulator environment
- **THEN** it MUST observe at least one combined shell `cd`, POSIX-style `..`, relative path operation, `/bin/pwd`, `fork`/`execve` inheritance, and user-visible output/error path
- **AND** the result MUST be decidable from runtime output, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: unavailable environment-dependent validation is recorded
- **WHEN** QEMU, Bochs, the x86_64 cross-toolchain, display/ROM dependencies, disk image configuration, or timeout oracle are unavailable
- **THEN** the corresponding cwd runtime validation MAY be skipped
- **AND** the validation record MUST identify the missing condition, substitute checks that were run, and remaining risk
