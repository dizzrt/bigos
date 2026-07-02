## ADDED Requirements

### Requirement: stream socket 非阻塞连接/接受/收发

BigOS SHALL 使 TCP stream socket 描述符复用既有 open file description 粒度的非阻塞标志，在标志置位且操作需要进入等待时返回确定性 would-block 而非阻塞：`connect` 握手未完成 MUST 返回 `-EINPROGRESS`；`accept` 无已完成连接 MUST 返回 `-EAGAIN`；`read` 无按序可读数据且非 EOF MUST 返回 `-EAGAIN`；`write` 发送路径无空间 MUST 返回 `-EAGAIN`。该 would-block 判定 MUST 与统一就绪查询（`poll_file`）同源。已写出部分字节的 `write` MUST 返回已写字节数而非 would-block。标志默认关闭时，stream socket 的 `connect`/`accept`/`read`/`write` MUST 沿用有界的 ordinary-context 阻塞语义。既有 pipe/tty/UDP socket 的非阻塞行为与 `O_NONBLOCK`/`fcntl` 语义 MUST NOT 改变。

#### Scenario: 非阻塞 connect 返回 EINPROGRESS

- **WHEN** 一个非阻塞 stream socket fd 调用 `connect` 且三次握手尚未完成
- **THEN** BigOS MUST 立即返回 `-EINPROGRESS`，MUST NOT 阻塞或忙等
- **AND** 该连接完成后其可写就绪查询 MUST 反映完成，随后 `read`/`write` 可用

#### Scenario: 非阻塞 accept 无连接返回 EAGAIN

- **WHEN** 一个非阻塞监听 stream socket fd 调用 `accept` 且已完成连接队列为空
- **THEN** BigOS MUST 立即返回 `-EAGAIN`，MUST NOT 阻塞或忙等

#### Scenario: 非阻塞 read 空连接返回 EAGAIN

- **WHEN** 一个非阻塞、已建立连接的 stream socket fd 在无按序可读数据且对端未关闭时被 `read`
- **THEN** BigOS MUST 返回 `-EAGAIN`，MUST NOT 在接收等待队列阻塞
- **AND** 对端写入数据后，对同一 fd 的非阻塞 `read` MUST 立即读到数据

#### Scenario: 非阻塞 write 满发送路径返回 EAGAIN

- **WHEN** 一个非阻塞 stream socket fd 在发送路径已满时被 `write`
- **THEN** 若尚未写出任何字节，BigOS MUST 返回 `-EAGAIN`，MUST NOT 在发送等待队列阻塞
- **AND** 若本次调用已写出部分字节后发送路径变满，BigOS MUST 返回已写出的字节数

#### Scenario: 清除标志后恢复阻塞语义

- **WHEN** stream socket fd 未设置非阻塞标志且当前操作需要等待
- **THEN** BigOS MUST 采用有界 ordinary-context 等待队列阻塞语义，MUST NOT 因本能力提前返回 would-block
- **AND** 既有 pipe/tty/UDP socket 非阻塞行为 MUST 保持不变
