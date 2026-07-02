## ADDED Requirements

### Requirement: stream socket 描述符纳入有界多路复用

BigOS SHALL 使 TCP stream socket 描述符（含监听 fd 与已连接 fd）参与既有有界多路复用 syscall（`SYS_POLL`），复用统一 fd 就绪查询（`vfs::poll_file`）与调度等待队列贡献（`poll_wait`）。stream socket 的就绪判定 MUST 与其 `read`/`write` would-block 判定同源且 level-triggered：监听 fd 在已完成连接队列非空时报告可读、已连接 fd 在有按序数据可读或可读到 EOF 时报告可读、在连接已建立且发送路径有空间时报告可写、在连接被复位时报告错误。`SYS_POLL` 的既有 ABI、定容上限（`POLL_MAX_FDS`）、超时语义与就绪模型 MUST NOT 因纳入 stream socket 而改变。

#### Scenario: 监听 stream socket 在 poll 中报告可 accept

- **WHEN** 用户程序把一个监听 stream socket fd 放入 `SYS_POLL` 集合并关注可读，且其已完成连接队列非空
- **THEN** BigOS MUST 在对应 pollfd 项回填可读就绪并计入就绪个数
- **AND** 随后对该 fd 的非阻塞 accept MUST 立即返回一个新连接 fd

#### Scenario: 已连接 stream socket 就绪与非阻塞读一致

- **WHEN** `SYS_POLL` 对一个已连接 stream socket fd 报告可读就绪
- **THEN** 随后对该 fd 的非阻塞 `read` MUST 立即成功（读到数据或 EOF），MUST NOT 返回 would-block

#### Scenario: stream socket 无就绪带超时阻塞后唤醒

- **WHEN** 一个线程对仅含无就绪 stream socket fd 的集合以正超时调用 `SYS_POLL`，随后某 stream socket 有入站连接完成或数据到达
- **THEN** BigOS MUST 让该线程阻塞让出而非忙等，并在其等待队列被唤醒后重新扫描就绪集合返回就绪个数
- **AND** MUST NOT 改变 `SYS_POLL` 既有 ABI、定容上限或超时语义

#### Scenario: 连接复位在 poll 中报告错误

- **WHEN** `SYS_POLL` 集合中的某 stream socket 连接被 RST 或重传超限复位
- **THEN** BigOS MUST 在对应 pollfd 项回填错误就绪并计入就绪个数，即使调用方未显式关注错误
