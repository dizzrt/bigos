## ADDED Requirements

### Requirement: SIGPIPE 信号

BigOS SHALL 以 append-only 方式向固定信号集合新增 `SIGPIPE`（编号 13，对齐 POSIX/Linux），默认动作为 Terminate，可被用户 `sigaction` 忽略（`SIG_IGN`）或安装 handler 捕获。`SIGPIPE` MUST 纳入既有每进程 pending 位图、阻塞掩码与处置表，且 MUST 复用既有信号投递路径（在返回用户态边界投递、不在 IRQ 上下文投递、不在内核态运行 handler）。新增 MUST NOT 改变任何既有信号的编号、默认动作或投递边界，`SIG_MAX` MUST 保持不超过 64（当前 31，13 在范围内，位图无需扩宽）。当向已关闭写方向的 stream socket 连接、或既有 pipe broken-pipe 语义路径（读端全关）写入时，BigOS MUST 向当前进程投递 `SIGPIPE` 并返回 `-EPIPE`；两条路径 MUST 复用同一 broken-pipe 投递语义。`SIGPIPE` 的投递 MUST 可被抑制：进程对 `SIGPIPE` 设置 `SIG_IGN`，或写调用携带 `MSG_NOSIGNAL`（见 stream-socket-interface 的 `send`）时，写操作 MUST 仅返回 `-EPIPE` 而不投递终止性信号。

#### Scenario: SIGPIPE 纳入固定信号集合

- **WHEN** 内核或用户代码引用 `SIGPIPE`
- **THEN** 它 MUST 映射到固定编号 13，且能用 `1 << (signo - 1)` 在每进程定长位图中表示
- **AND** 既有信号编号、默认动作与投递边界 MUST 保持不变

#### Scenario: broken-pipe 写投递 SIGPIPE 且默认终止

- **WHEN** 一个进程未忽略/未捕获 `SIGPIPE`，向已关闭写方向的 stream socket 连接或读端全关的 pipe 写入触发 broken-pipe
- **THEN** BigOS MUST 通过既有信号投递路径投递 `SIGPIPE`，默认动作 Terminate 经既有 exit/fault-to-reaper 生命周期终止该进程
- **AND** 投递 MUST 发生在返回用户态边界，MUST NOT 在 IRQ 上下文或内核临界区投递

#### Scenario: 忽略 SIGPIPE 时仅返回 EPIPE

- **WHEN** 一个进程对 `SIGPIPE` 设置 `SIG_IGN`，随后向已关闭写方向的连接或 pipe 写入
- **THEN** 写操作 MUST 返回 `-EPIPE`，且进程 MUST NOT 因该写入被终止
- **AND** `SIGPIPE` 的 pending 处理 MUST 遵循既有忽略动作语义（清除 pending，不改变用户上下文）

#### Scenario: MSG_NOSIGNAL 抑制 SIGPIPE

- **WHEN** 一个进程以携带 `MSG_NOSIGNAL` 的 `send` 向已关闭写方向的 stream socket 写入，即使未对 `SIGPIPE` 设置 `SIG_IGN`
- **THEN** 写操作 MUST 仅返回 `-EPIPE`，MUST NOT 投递 `SIGPIPE`
- **AND** 未携带 `MSG_NOSIGNAL` 且未忽略 `SIGPIPE` 的写 MUST 仍按默认 Terminate 投递

### Requirement: 统一 broken-pipe 投递辅助

BigOS SHALL 把 broken-pipe 的「返回 `-EPIPE` 并按抑制条件决定是否投递 `SIGPIPE`」逻辑收敛到单一内核内部辅助，供 pipe 写关闭端与 stream socket 写已关闭对端两条路径共用。该辅助 MUST 在唯一一处判定抑制条件（进程对 `SIGPIPE` 设置 `SIG_IGN`，或写调用携带 `MSG_NOSIGNAL`），并复用既有信号投递路径（ordinary/返回用户态边界投递，不在 IRQ 上下文投递）。两条路径的返回码、信号投递与抑制行为 MUST 一致，MUST NOT 各自维护可能漂移的判定。

#### Scenario: pipe 与 stream socket 行为同源

- **WHEN** pipe 写关闭端与 stream socket 写已关闭对端分别触发 broken-pipe
- **THEN** 两条路径 MUST 经同一 broken-pipe 投递辅助产生一致的返回码（`-EPIPE`）与信号投递结果
- **AND** 抑制条件（`SIG_IGN` 或 `MSG_NOSIGNAL`）的判定 MUST 只有一处实现

#### Scenario: 抑制条件在辅助中集中判定

- **WHEN** 调用方以「是否抑制」参数（源自 `SIG_IGN` 或 `MSG_NOSIGNAL`）请求 broken-pipe 投递
- **THEN** 辅助 MUST 据此决定仅返回 `-EPIPE` 还是同时投递默认终止性 `SIGPIPE`
- **AND** MUST NOT 在 pipe 或 stream socket 各自路径中重复实现该判定
