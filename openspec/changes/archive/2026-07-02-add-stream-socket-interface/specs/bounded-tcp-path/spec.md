## MODIFIED Requirements

### Requirement: TCP 连接建立

BigOS SHALL 实现有界三次握手连接建立，覆盖主动打开（发送 SYN → `SYN_SENT` → 收 SYN,ACK → 发 ACK → `ESTABLISHED`）与被动打开。被动打开 MUST 采用 Linux/BSD 风格 listener 与子连接分离的半连接(SYN)+全连接(accept)双队列模型：一个处于 `LISTEN` 的本地端口收到匹配 SYN 时，MUST 派生一个独立的子连接 TCB（`LISTEN` → 派生子 TCB 并登记半连接队列 → 发 SYN,ACK → 子 TCB 进入 `SYN_RECEIVED` → 收 ACK → 子 TCB 进入 `ESTABLISHED` 并移入全连接队列），而 listener TCB MUST 保持 `LISTEN` 以继续接受后续入站连接。连接建立 MUST 在 ordinary（可阻塞、非 IRQ）内核上下文推进，MUST 正确初始化双向序号空间，并对非法或不匹配的握手段以确定性状态处理。子连接 TCB MUST 从既有定容 TCB 池分配；当无空闲槽、半连接队列已满或全连接队列已满时，新入站连接 MUST 以确定性状态拒绝/丢弃，MUST NOT 无界扩张或复用 listener 自身作为单一连接。

#### Scenario: 主动打开完成三次握手

- **WHEN** 内核内部路径对一个本机地址目的发起主动打开
- **THEN** BigOS MUST 发送 SYN 进入 `SYN_SENT`，在收到匹配的 SYN,ACK 后发送 ACK 并进入 `ESTABLISHED`
- **AND** MUST 以对端初始序号初始化 `rcv_nxt` 并推进本地 `snd_nxt`/`snd_una`，递增确定性连接建立诊断计数

#### Scenario: 被动打开派生子连接完成三次握手

- **WHEN** 一个处于 `LISTEN` 的本地端口收到匹配的 SYN
- **THEN** BigOS MUST 派生一个独立的子连接 TCB、对其发送 SYN,ACK 进入 `SYN_RECEIVED`，在收到匹配 ACK 后使该子连接进入 `ESTABLISHED`，同时 listener MUST 保持 `LISTEN`
- **AND** MUST 正确初始化子连接双向序号空间并递增确定性连接建立诊断计数

#### Scenario: listener 接受多个连接

- **WHEN** 一个处于 `LISTEN` 的本地端口先后收到来自不同四元组的多个匹配 SYN，且定容 TCB 池与半/全连接队列有可用容量
- **THEN** BigOS MUST 为每个入站连接派生独立子连接 TCB 并分别完成握手，listener MUST 始终保持 `LISTEN`
- **AND** 各已完成子连接 MUST 移入 listener 名下的全连接队列供上层取用

#### Scenario: 拒绝非法或不匹配握手段

- **WHEN** 到达的握手段序号/确认号与连接状态不匹配、标志组合非法或校验和失败
- **THEN** BigOS MUST 以确定性状态丢弃或复位（视状态发送 RST），MUST NOT 迁移到 `ESTABLISHED`
- **AND** MUST 递增确定性 TCP 丢弃/复位诊断计数

#### Scenario: 子连接资源或队列耗尽

- **WHEN** listener 收到匹配 SYN 但定容 TCB 池无空闲槽或 listener 的已完成连接队列已满
- **THEN** BigOS MUST 以确定性状态拒绝/丢弃该入站连接并递增确定性 TCP 丢弃诊断计数
- **AND** MUST NOT 无界扩张连接表/队列，MUST NOT 复用 listener 自身作为该连接

## ADDED Requirements

### Requirement: 半连接/全连接双队列与连接级就绪等待

BigOS SHALL 为被动打开提供 listener 名下的两条定容队列：半连接队列（SYN queue，容纳 `SYN_RECEIVED` 握手中的子连接）与全连接队列（accept queue，容纳已完成三次握手 `ESTABLISHED` 但尚未被上层取走的子连接），并提供内核内部入口从全连接队列取出一个已完成连接。两队列容量 MUST 为编译期定容上界。定容 TCB 池容量 MUST 足以在保持 listener 的同时容纳半连接、全连接与主动连接的并发占用（含本机地址闭环下 listener+client+child 同池占用），并以编译期断言固化该容量不变式。BigOS SHALL 为 TCB 提供连接级等待队列，使数据到达、连接进入 `ESTABLISHED`、有新入站连接可取、或连接进入错误/复位状态时唤醒等待者，供上层 fd readiness 与有界阻塞路径复用。所有取连接、入队与唤醒 MUST 在 ordinary 内核上下文进行，MUST NOT 从 IRQ 上下文操作队列或等待队列。

#### Scenario: 从 listener 取出一个已完成连接

- **WHEN** 内核内部入口请求从一个 listener 的全连接队列取出连接且队列非空
- **THEN** BigOS MUST 返回一个 `ESTABLISHED` 子连接 TCB 并将其从全连接队列移除
- **AND** 队列为空时 MUST 返回确定性无连接状态，MUST NOT 阻塞在 IRQ 上下文或忙等

#### Scenario: 连接级等待队列唤醒

- **WHEN** 一个等待者登记在某连接（或 listener）的连接级等待队列上，随后该连接收到按序数据、进入 `ESTABLISHED`、有新入站连接完成、或被复位
- **THEN** BigOS MUST 通过该等待队列唤醒等待者
- **AND** 唤醒 MUST 发生在 ordinary 上下文，MUST NOT 从 IRQ 上下文触发连接缓冲操作

#### Scenario: 半连接/全连接队列有界

- **WHEN** 握手中的子连接达到半连接队列上界，或已完成连接持续入队而上层未及时取走达到全连接队列上界
- **THEN** BigOS MUST 以确定性状态处理超额入站连接/握手，MUST NOT 无界扩张任一队列

#### Scenario: TCB 池容量不变式

- **WHEN** 编译内核并运行 TCB 池与队列容量的源级断言
- **THEN** 定容 TCB 池容量 MUST 满足「listener + 半连接队列 + 全连接队列 + 主动连接预算」的不变式
- **AND** 该不变式 MUST 由编译期断言守护，MUST NOT 因保持 listener 而使单条本机连接建立即耗尽连接池
