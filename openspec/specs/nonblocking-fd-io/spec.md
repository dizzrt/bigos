# nonblocking-fd-io Specification

## Purpose

本规格定义 BigOS 有界非阻塞描述符行为的用户可见与内核内可观测契约：为 pipe、tty、socket 描述符提供 open file description 粒度的非阻塞标志，使读/写/接收在数据未就绪时返回确定性 would-block 状态（`-EWOULDBLOCK`/`-EAGAIN`）而非阻塞，复用既有 readiness 谓词与阻塞原语。该能力保持有界，不暗示完整 POSIX `O_NONBLOCK` 语义，也不覆盖常规文件、块设备、目录等恒定就绪/同步完成的描述符类型。

## Requirements

### Requirement: open file description 粒度的非阻塞标志

BigOS SHALL 在 open file description（内核 `vfs::File`）粒度提供一个非阻塞标志，默认关闭。该标志 MUST 通过既有 `dup`/`fork` 引用计数路径随同一打开文件描述共享，而非按进程局部 fd 表项独立复制。标志默认关闭时，所有既有阻塞读写语义 MUST 保持不变。该能力 MUST NOT 暗示完整 POSIX `O_NONBLOCK`，也 MUST NOT 改变常规文件、块设备、目录等恒定就绪/同步完成描述符的可观察行为。

#### Scenario: 默认阻塞语义不变

- **WHEN** 一个 pipe、tty 或 socket 描述符在未设置非阻塞标志时被读/写/接收且数据未就绪
- **THEN** BigOS MUST 沿用既有阻塞或既有有界等待语义，MUST NOT 因本能力而提前返回 would-block

#### Scenario: 标志随打开文件描述共享

- **WHEN** 持有某非阻塞描述符的进程对其 `dup`/`dup2` 或 `fork`
- **THEN** 复制出的描述符 MUST 共享同一打开文件描述的非阻塞标志状态
- **AND** 通过任一描述符切换该标志 MUST 对共享同一打开文件描述的其它描述符一致可见

#### Scenario: 终端 fd 0/1/2 非阻塞标志联动

- **WHEN** 进程的标准 fd 0/1/2 共享同一终端打开文件描述，且对其中一个终端 fd 设置非阻塞标志
- **THEN** 共享同一打开文件描述的其它终端 fd（含 `dup` 副本）MUST 通过 `F_GETFL` 一致观察到该非阻塞位
- **AND** 这些终端 fd 的读路径 MUST 同步呈现非阻塞 would-block 行为，符合 POSIX 共享 OFD 语义

#### Scenario: 恒定就绪描述符不受影响

- **WHEN** 对常规文件、块设备或目录描述符设置非阻塞标志后进行读写
- **THEN** BigOS MUST 维持其既有同步完成语义，MUST NOT 产生本能力定义之外的 would-block 返回

### Requirement: 非阻塞读写返回确定性 would-block

BigOS SHALL 在描述符被标记为非阻塞且当前操作需要进入等待时，使读、写与接收返回确定性的 would-block 状态（用户可见 `-EWOULDBLOCK`，等价 `-EAGAIN`），而 MUST NOT 进入调度等待队列或执行忙等轮转。该 would-block 判定 MUST 与统一就绪模型（`poll_file`）及既有阻塞谓词同源：当且仅当就绪查询报告该方向不就绪时，非阻塞操作才返回 would-block。已写出部分字节的写 MUST 返回已写字节数而非 would-block。

#### Scenario: 非阻塞读空管道返回 would-block

- **WHEN** 一个非阻塞 pipe 读端在管道为空且写端仍打开时被读取
- **THEN** BigOS MUST 立即返回 would-block 状态，MUST NOT 在读等待队列阻塞或忙等
- **AND** 在写端写入数据后，对同一描述符的非阻塞读 MUST 立即读到数据并成功返回

#### Scenario: 非阻塞写满管道返回 would-block

- **WHEN** 一个非阻塞 pipe 写端在管道已满且读端仍打开时被写入
- **THEN** 若尚未写出任何字节，BigOS MUST 立即返回 would-block 状态，MUST NOT 在写等待队列阻塞
- **AND** 若本次调用已写出部分字节后缓冲变满，BigOS MUST 返回已写出的字节数

#### Scenario: 非阻塞 tty 无输入返回 would-block

- **WHEN** 一个非阻塞终端描述符在无可用输入记录时被读取
- **THEN** BigOS MUST 立即返回 would-block 状态，MUST NOT 在输入等待队列阻塞
- **AND** 该 poll 只读判定 MUST NOT 出队任何输入记录或改动输入环状态

#### Scenario: 非阻塞 socket 无数据返回 EAGAIN

- **WHEN** 一个非阻塞且已绑定本地端口的 socket 描述符调用接收，且经过单次有界 RX 推进后仍无可用 datagram
- **THEN** BigOS MUST 返回 `-EAGAIN`，MUST NOT 执行有界 poll-and-yield 等待轮次或永久阻塞
- **AND** 阻塞 socket 描述符在相同无数据条件下的有界等待轮次与返回码 MUST 保持不变

### Requirement: would-block 与就绪查询一致性

BigOS SHALL 保证非阻塞读写的 would-block 行为与统一 fd 就绪查询互为表里：对同一描述符在同一时刻，`poll_file` 报告可读当且仅当非阻塞读不会返回 would-block；报告可写当且仅当非阻塞写不会返回 would-block。该一致性 MUST 通过各后端复用同一就绪/阻塞谓词实现，MUST NOT 由各路径各自维护可能漂移的判定。

#### Scenario: 就绪查询与非阻塞读一致

- **WHEN** 统一就绪查询对某描述符报告可读
- **THEN** 对该描述符的非阻塞读 MUST 立即成功（读到数据或 EOF），MUST NOT 返回 would-block

#### Scenario: 不就绪查询与非阻塞读一致

- **WHEN** 统一就绪查询对某描述符报告不可读
- **THEN** 对该描述符的非阻塞读 MUST 返回 would-block，MUST NOT 阻塞或忙等

### Requirement: 默认关闭验证与默认启动独立性

BigOS SHALL 通过一个默认关闭的运行期 smoke 验证非阻塞描述符行为：覆盖 pipe/tty/socket 在置位与清除非阻塞标志下的 would-block 与恢复阻塞语义、`F_GETFL`/`F_SETFL` 往返一致、以及 would-block 与就绪查询的一致性，并发出确定性通过/失败 marker。默认启动 MUST 与该能力无关：非阻塞标志默认关闭、smoke 默认关闭，默认 boot 进入 shell 的行为 MUST NOT 改变。验证依赖不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: 非阻塞 smoke 闭环通过

- **WHEN** 启用非阻塞验证 build switch 并在受控环境运行该 smoke
- **THEN** smoke MUST 覆盖 pipe/tty/socket 的非阻塞 would-block、清除标志后恢复阻塞、`F_GETFL`/`F_SETFL` 往返与就绪一致性，并发出确定性通过/失败 marker

#### Scenario: 默认启动不依赖非阻塞能力

- **WHEN** 非阻塞验证 switch 关闭
- **THEN** 默认启动、fd/VFS、pipe、tty、socket 与 userland baseline MUST 维持既有阻塞语义并正常进入 shell

#### Scenario: 验证不可用时记录跳过

- **WHEN** QEMU、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断，MUST NOT 声称运行成功
