## ADDED Requirements

### Requirement: waitpid 有界匹配与状态观察
BigOS SHALL provide bounded `wait`/`waitpid` behavior that lets a parent observe direct child completion by exact PID or by the documented any-child form, write a deterministic bounded status when a status pointer is provided, and reject unsupported options without implying complete POSIX wait semantics.

#### Scenario: waitpid 等待指定子进程
- **WHEN** a parent invokes `waitpid` for a direct child PID that has already exited, faulted, or terminated by signal
- **THEN** BigOS MUST return that child PID and publish the documented bounded completion status to the caller-visible status location when provided
- **AND** the child MUST become eligible for the existing safe reap path after the status is consumed

#### Scenario: waitpid 等待任意子进程
- **WHEN** a parent invokes the documented any-child wait form and at least one direct child is waitable
- **THEN** BigOS MUST select one waitable direct child deterministically enough for validation, return its PID, and publish its bounded status when requested
- **AND** it MUST NOT reap unrelated processes or children of another parent

#### Scenario: waitpid options 保持有界
- **WHEN** a parent invokes `waitpid` with options outside the supported bounded subset
- **THEN** BigOS MUST return `-EINVAL`
- **AND** it MUST NOT block, reap a child, or modify the caller-visible status storage

#### Scenario: 无匹配子进程返回确定性错误
- **WHEN** a process waits for a PID that is not its direct child, has already been fully reaped, or cannot match the documented any-child form
- **THEN** BigOS MUST return a deterministic no-child or no-match error distinct from `-EINVAL`, such as `-ECHILD` when that errno is available in the bounded errno set
- **AND** the calling shell or user program MUST remain runnable and able to issue later syscalls

### Requirement: 进程终止状态编码可组合
BigOS SHALL encode normal exit, user fault termination, exec no-return failure, and signal termination in a bounded process status format that can be consumed by libc wrappers, shell status tracking, and behavior validation without requiring complete POSIX wait macros or job-control states.

#### Scenario: 正常退出状态可观察
- **WHEN** a child exits with a bounded exit code and the parent waits for it
- **THEN** the parent-visible status MUST distinguish normal exit from fault or signal termination
- **AND** the low bounded exit value MUST be recoverable by the documented libc or shell helper path

#### Scenario: signal 终止状态可观察
- **WHEN** a child is terminated by a supported signal default action
- **THEN** the parent-visible status MUST distinguish signal termination from normal exit
- **AND** the terminating signal number MUST be recoverable within the documented bounded status format

#### Scenario: 状态编码不声明完整 POSIX
- **WHEN** documentation, headers, tests, or shell behavior describe process status
- **THEN** they MUST describe it as the BigOS bounded wait status contract
- **AND** they MUST NOT imply stopped/continued states, process groups, sessions, job control, core dumps, or complete POSIX wait macro coverage
