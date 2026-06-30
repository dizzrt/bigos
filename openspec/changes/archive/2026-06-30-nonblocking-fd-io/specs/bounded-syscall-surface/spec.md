## MODIFIED Requirements

### Requirement: 有界 fd 控制 primitive

BigOS SHALL expose bounded fd-control primitives sufficient for small user programs to query and set close-on-exec state on process-local descriptors, duplicate descriptors with `F_DUPFD`, and query/set the bounded `O_NONBLOCK` status flag on the open file description via `F_GETFL`/`F_SETFL`. The fd-control surface MUST preserve fd table ownership, lowest-available descriptor allocation, open file object reference semantics, and deterministic bad-fd behavior. `F_GETFL` MUST report the access mode together with the `O_NONBLOCK` bit; `F_SETFL` MUST set the open file description's nonblocking flag from the `O_NONBLOCK` bit and MUST ignore all other bits in the argument (access-mode bits, creation bits, and any unimplemented status bits) without error, returning success. It MUST NOT alter the access mode or close-on-exec state. It MUST NOT implement complete POSIX `fcntl`, file locking, signal-driven/async I/O (`O_ASYNC`), `F_DUPFD_CLOEXEC`, or descriptor passing.

#### Scenario: 设置 close-on-exec

- **WHEN** a user process requests close-on-exec on a valid open descriptor through the bounded fd-control interface
- **THEN** BigOS MUST mark that fd table entry so a successful `execve` commit closes it before entering the new user image
- **AND** the operation MUST NOT close the descriptor immediately

#### Scenario: 查询 close-on-exec

- **WHEN** a user process queries close-on-exec state for a valid open descriptor
- **THEN** BigOS MUST return whether the fd table entry is marked close-on-exec
- **AND** it MUST leave the descriptor, open file offset, and open file reference unchanged

#### Scenario: F_DUPFD 分配最低可用 descriptor

- **WHEN** a user process requests `F_DUPFD` on a valid descriptor with a supported minimum descriptor value
- **THEN** BigOS MUST allocate and return the lowest available descriptor greater than or equal to that minimum
- **AND** the new descriptor MUST reference the same open file object, share the same open file offset semantics as existing duplication paths, and start with close-on-exec cleared

#### Scenario: F_DUPFD 拒绝非法起点或容量不足

- **WHEN** a user process requests `F_DUPFD` with an invalid minimum descriptor, an invalid source descriptor, or no available descriptor within the bounded fd table limits
- **THEN** BigOS MUST fail with a deterministic errno
- **AND** it MUST NOT allocate a partial descriptor, leak an open file reference, or mutate the source descriptor flags

#### Scenario: F_GETFL 返回访问模式与非阻塞位

- **WHEN** a user process calls `F_GETFL` on a valid open descriptor
- **THEN** BigOS MUST return the access mode bits synthesized from the open file's readable/writable state ORed with `O_NONBLOCK` when the open file description's nonblocking flag is set
- **AND** the query MUST leave the descriptor, open file offset, open file reference, and close-on-exec state unchanged

#### Scenario: F_SETFL 切换非阻塞位

- **WHEN** a user process calls `F_SETFL` on a valid open descriptor with or without the `O_NONBLOCK` bit in the argument
- **THEN** BigOS MUST set the open file description's nonblocking flag to match the `O_NONBLOCK` bit and return success
- **AND** it MUST NOT change the access mode, close-on-exec state, or the open file offset

#### Scenario: F_SETFL 忽略访问模式与不支持位

- **WHEN** a user process calls `F_SETFL` with an argument that also carries access-mode bits, creation bits, or unimplemented status bits (such as the common idiom passing the result of `F_GETFL` ORed with `O_NONBLOCK`)
- **THEN** BigOS MUST apply only the `O_NONBLOCK` bit, ignore every other bit without error, and return success
- **AND** it MUST NOT fail with `-EINVAL` for the carried access-mode bits, MUST NOT change the access mode, and MUST NOT mutate close-on-exec state

#### Scenario: fd 控制拒绝 bad fd

- **WHEN** a user process applies fd-control operations to a closed, out-of-range, or otherwise invalid descriptor
- **THEN** BigOS MUST fail with a deterministic bad-fd errno
- **AND** it MUST NOT access freed file state or allocate a replacement descriptor
