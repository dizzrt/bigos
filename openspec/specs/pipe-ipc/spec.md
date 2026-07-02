## Purpose

定义 BigOS 管道与 fd 复制能力：`pipe` 创建一对内核有界环形缓冲连接的读/写端打开文件对象，
写满阻塞、读空阻塞（复用现有 wait queue/blocking 原语，仅在可阻塞进程上下文），写端全关 ->
读端 EOF、读端全关 -> 写端 `EPIPE`，`dup`/`dup2` 复制描述符并共享底层打开文件对象（offset 与
端引用计数共享），并定义 fork/exec/退出下的引用计数与生命周期语义。该能力不引入命名管道
（FIFO）、`mknod` 或 socket 语义，并以默认关闭的运行时 smoke 验证。

## Requirements

### Requirement: pipe 创建一对相连读写端

BigOS SHALL 提供 `pipe`，创建一个内核有界环形缓冲与一对相连的读端/写端打开文件对象，并把两个进程 fd 写回调用方。读端 MUST 只读、写端 MUST 只写，缓冲容量 MUST 有界，且 MUST NOT 引入命名管道（FIFO）、`mknod` 或 socket 语义。

#### Scenario: pipe 返回读写两个 fd

- **WHEN** 进程调用 `pipe` 且其 fd 表有至少两个可用槽位、内核内存充足
- **THEN** BigOS MUST 创建一个有界环形缓冲与读端/写端文件对象，分配两个最低可用 fd 并写回调用方
- **AND** 写入写端的数据 MUST 可从读端按 FIFO 顺序读出

#### Scenario: pipe 资源不足确定性失败

- **WHEN** 进程调用 `pipe` 但 fd 表容量不足或内核内存分配失败
- **THEN** BigOS MUST 返回确定性错误（如 `-EMFILE` 或 `-ENOMEM`），MUST 释放任何未发布的对象，MUST NOT panic

### Requirement: 管道阻塞读写与上下文边界

BigOS SHALL 在管道读空且写端仍打开时阻塞读者、在写满且读端仍打开时阻塞写者，复用现有 wait queue/blocking 原语并仅在可阻塞进程上下文进行。对端动作 MUST 确定性唤醒等待者；阻塞读写 MUST NOT 在 IRQ 或不可阻塞上下文进行。

#### Scenario: 读空阻塞，写入唤醒

- **WHEN** 读者从空管道读且写端仍打开
- **THEN** 读者 MUST 在管道读等待队列阻塞，直到写者写入后被确定性唤醒并读到数据

#### Scenario: 写满阻塞，读出唤醒

- **WHEN** 写者向已满管道写且读端仍打开
- **THEN** 写者 MUST 在管道写等待队列阻塞，直到读者读出腾出空间后被确定性唤醒

#### Scenario: 不可阻塞上下文拒绝管道阻塞

- **WHEN** 管道读/写从 IRQ、调度临界区或 preemption-disable 的不可阻塞上下文被调用且需要阻塞
- **THEN** BigOS MUST 确定性失败，MUST NOT 进入等待状态

### Requirement: 管道 EOF 与 EPIPE 语义

BigOS SHALL 在所有写端关闭后令读端读到 EOF（读返回 0），在所有读端关闭后令写端写返回确定性 `-EPIPE` 并向写者投递 `SIGPIPE`（默认动作终止）。端引用计数 MUST 精确管理，最后一个端关闭时 MUST 确定性唤醒对端。`SIGPIPE` 的投递 MUST 复用既有信号投递路径并与 stream socket 的 broken-pipe 语义一致；当写者进程对 `SIGPIPE` 设置 `SIG_IGN` 时写 MUST 仅返回 `-EPIPE` 而不终止。

#### Scenario: 写端全关读到 EOF

- **WHEN** 管道所有写端已关闭且缓冲已被读空
- **THEN** 读者的读 MUST 返回 0（EOF），MUST NOT 阻塞

#### Scenario: 读端全关写返回 EPIPE 并投递 SIGPIPE

- **WHEN** 管道所有读端已关闭，写者尝试写入
- **THEN** BigOS MUST 返回确定性 `-EPIPE` 并向写者投递 `SIGPIPE`（默认动作终止）
- **AND** 当写者进程对 `SIGPIPE` 设置 `SIG_IGN` 时 MUST 仅返回 `-EPIPE` 而不终止进程

### Requirement: fd 复制与共享语义

BigOS SHALL 提供 `dup`/`dup2` 复制文件描述符，新旧 fd MUST 指向同一打开文件对象并共享 offset 与底层端引用计数。`dup2` MUST 在目标 fd 已打开时先关闭它；引用计数 MUST 保证每个 fd 关闭时底层对象引用精确递减一次。

#### Scenario: dup 复制到最低可用 fd

- **WHEN** 进程对一个有效 fd 调用 `dup`
- **THEN** BigOS MUST 分配最低可用 fd 指向同一打开文件对象，并增加其引用计数

#### Scenario: dup2 复制到指定 fd

- **WHEN** 进程对有效 oldfd 与目标 newfd 调用 `dup2` 且 newfd 已打开
- **THEN** BigOS MUST 先关闭 newfd 原有对象一次，再令 newfd 指向 oldfd 的打开文件对象并增加引用计数
- **AND** 共享同一对象的多个 fd MUST 共享同一 offset

#### Scenario: dup 非法 fd 被拒绝

- **WHEN** 进程对一个未使用、已关闭或越界的 fd 调用 `dup`/`dup2`
- **THEN** BigOS MUST 返回确定性 `-EBADF`，MUST NOT 修改 fd 表

### Requirement: 管道与 fork/exec/退出生命周期

BigOS SHALL 把管道端文件对象纳入进程生命周期：`fork` MUST 让子进程继承管道端 fd 并增加端引用计数，`exec` MUST 按 close-on-exec 规则关闭或保留管道端 fd，进程退出/被回收时 MUST 关闭所有剩余管道端 fd 且端引用计数归零时唯一回收管道对象。

#### Scenario: fork 继承管道端并增计数

- **WHEN** 持有管道端 fd 的进程 `fork`
- **THEN** 子进程 MUST 继承未标记 close-on-exec 的管道端 fd，对应端引用计数 MUST 增加

#### Scenario: 退出关闭管道端并回收

- **WHEN** 持有管道端 fd 的进程退出或被回收
- **THEN** BigOS MUST 关闭其所有管道端 fd 各一次，端引用计数归零时 MUST 唯一回收管道对象并确定性唤醒对端

### Requirement: 管道能力验证可复现

BigOS SHALL 通过默认关闭的运行时 smoke 与源码/行为断言验证管道与 fd 复制。验证 MUST 记录工具链与模拟器可用性、串口 marker、跳过的用例与残余风险，且默认启动 marker 与既有 smoke 矩阵 MUST 保持不变。管道 smoke MUST 覆盖读端全关写返回 `-EPIPE` 且默认投递 `SIGPIPE` 终止（未忽略时）与 `SIG_IGN` 下仅 `-EPIPE` 的行为。

#### Scenario: pipe smoke 发射有界 marker

- **WHEN** 启用 `pipe_smoke`（`BIGOS_PIPE_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「跨进程写读 FIFO 一致」「读空阻塞 + 写入唤醒」「写端全关读 EOF」「读端全关写 EPIPE 且投递 SIGPIPE」「`SIG_IGN` 下写仅 EPIPE 不终止」「`dup`/`dup2` 共享 offset」并发射 `BIGOS_PIPE_PASSED`/`BIGOS_PIPE_FAILED`
- **AND** 该开关 MUST 默认关闭；QEMU/Bochs 或交叉工具链不可用时 MUST 记录为跳过验证而非声称通过
