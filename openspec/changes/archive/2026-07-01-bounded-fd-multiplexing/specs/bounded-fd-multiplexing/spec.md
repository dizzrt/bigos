## ADDED Requirements

### Requirement: 有界多路复用 syscall 消费面

BigOS SHALL 提供一个用户可见的有界多路复用 syscall（poll 风格），对一个定容的描述符集合带毫秒级超时等待，并逐项报告可读/可写/错误就绪状态。该 syscall MUST 接受一个用户态 pollfd 数组（每项含描述符、请求关注的事件位、内核回填的就绪事件位）与一个描述符个数、一个毫秒超时值。返回值 MUST 是就绪描述符的个数（超时且无就绪时为 0），或确定性负 errno。该能力 MUST 保持有界：MUST NOT 声称完整 POSIX `poll`/`select`/`epoll`/`ppoll` 语义，MUST NOT 支持无界或动态增长的描述符集合，MUST NOT 引入边缘触发、事件通知对象、带外/优先级事件或信号中断的复杂 restart 语义。

#### Scenario: 集合内某描述符就绪时返回就绪计数

- **WHEN** 用户程序对一个包含 pipe/socket/tty 描述符的定容集合调用该 syscall，且其中至少一个描述符满足其请求的可读或可写条件
- **THEN** BigOS MUST 在对应 pollfd 项回填就绪事件位，并返回就绪描述符的个数
- **AND** 本次调用 MUST NOT 出队数据或改变任一描述符的偏移/打开状态

#### Scenario: 定容集合上限确定性拒绝

- **WHEN** 用户程序传入的描述符个数超过该 syscall 文档化的定容上限
- **THEN** BigOS MUST 以确定性 errno 失败，MUST NOT 阻塞或部分处理该集合

#### Scenario: 非法用户数组指针失败

- **WHEN** 用户程序传入的 pollfd 数组指针不可访问或长度越界
- **THEN** BigOS MUST 以确定性 `-EFAULT` 类错误失败，MUST NOT 阻塞或写入用户内存

### Requirement: 就绪判定复用统一就绪模型且 level-triggered

多路复用 syscall SHALL 对每个被监听的有效描述符复用统一 fd 就绪查询（`vfs::poll_file`）产生就绪位，并把内核内可读/可写/错误就绪位映射为用户可见事件位。该映射 MUST 是 level-triggered：只要就绪条件持续满足就持续报告。就绪判定 MUST 与非阻塞读写的 would-block 判定、阻塞谓词三者同源，MUST NOT 由多路复用路径各自维护可能漂移的判定。请求关注可读/可写的事件位 MUST 与查询结果求交后回填；错误/挂断类就绪 MUST 在对应条件下回填，即使调用方未显式请求。

#### Scenario: 就绪位与非阻塞读一致

- **WHEN** 多路复用 syscall 对某描述符报告可读就绪
- **THEN** 随后对该描述符的非阻塞读 MUST 立即成功（读到数据或 EOF），MUST NOT 返回 would-block

#### Scenario: 不就绪与非阻塞读一致

- **WHEN** 多路复用 syscall 未对某描述符报告可读就绪
- **THEN** 随后对该描述符的非阻塞读 MUST 返回 would-block

#### Scenario: 错误就绪无条件回填

- **WHEN** 一个被监听描述符的统一就绪查询报告错误位（例如读端关闭的 pipe 写端）
- **THEN** BigOS MUST 在该 pollfd 项回填错误事件位并计入就绪个数，即使调用方未在请求事件位中显式关注

### Requirement: 多路复用带超时阻塞复用调度等待队列

多路复用 syscall SHALL 在集合内无描述符就绪且允许阻塞时，复用调度等待队列让当前线程真正阻塞让出，而 MUST NOT 忙等轮转。当前线程 MUST 能在同一次阻塞中同时登记到被监听描述符各自后端的就绪等待队列上；集合中任一描述符经其生产者路径唤醒、或超时到期时，线程 MUST 被唤醒并重新扫描就绪集合。超时语义 MUST 确定：正超时到期且无就绪返回 0；零超时退化为单次就绪扫描立即返回；负超时表示无限等待。唤醒后 MUST NOT 向用户泄漏调度器私有等待常量。

#### Scenario: 无就绪带正超时阻塞后返回 0

- **WHEN** 用户程序对一个当前均不就绪的描述符集合以正毫秒超时调用该 syscall，且在超时窗口内无描述符变就绪
- **THEN** BigOS MUST 让当前线程阻塞让出而非忙等，并在超时到期后返回 0
- **AND** 本次调用 MUST NOT 出队数据或改变任一描述符状态

#### Scenario: 阻塞中某描述符就绪唤醒

- **WHEN** 当前线程因集合无就绪而带超时阻塞，随后集合中某描述符的生产者路径（例如 pipe 写入、socket 收到 datagram、tty 输入到达）使其变可读
- **THEN** BigOS MUST 唤醒该线程，重新扫描就绪集合，仅在已就绪的 pollfd 项回填就绪事件位，并返回就绪个数

#### Scenario: 零超时非阻塞探测

- **WHEN** 用户程序以零超时调用该 syscall
- **THEN** BigOS MUST 只做一次就绪扫描并立即返回就绪个数（可能为 0），MUST NOT 进入等待队列或忙等

### Requirement: 坏描述符逐项标记而非整调用失败

多路复用 syscall SHALL 对集合中单个非法或未打开的描述符逐项标记无效就绪事件位，并把该项计入就绪个数，而 MUST NOT 因单个坏描述符使整个调用失败。负描述符项 MUST 被忽略（其回填就绪位清零，且不参与阻塞与计数）。

#### Scenario: 坏 fd 逐项标记

- **WHEN** 集合中某项引用一个已关闭、越界或从未打开的描述符
- **THEN** BigOS MUST 在该 pollfd 项回填无效描述符事件位并计入就绪个数，MUST NOT 使整个调用返回错误或访问已释放的文件状态

#### Scenario: 负描述符项被忽略

- **WHEN** 集合中某项的描述符为负值
- **THEN** BigOS MUST 忽略该项（其回填就绪位清零），MUST NOT 因该项而报告就绪或阻塞

### Requirement: 多路复用默认关闭验证与默认启动独立性

多路复用能力 SHALL 通过一个默认关闭的运行期 smoke 验证：覆盖无就绪带超时阻塞后返回 0、某描述符就绪后唤醒且仅报告该描述符、就绪位与非阻塞读一致、坏描述符逐项标记、以及定容上限的确定性拒绝，并发出确定性通过/失败 marker。默认启动 MUST 与该能力无关：smoke 默认关闭、多路复用仅在用户显式调用时生效，默认 boot 进入 shell 的行为 MUST NOT 改变。验证依赖不可用时 MUST 记录跳过原因与剩余风险。

#### Scenario: 多路复用 smoke 闭环通过

- **WHEN** 启用多路复用验证 build switch 并在受控 QEMU headless 环境运行该 smoke
- **THEN** smoke MUST 覆盖超时阻塞返回 0、就绪唤醒、就绪与非阻塞读一致、坏 fd 标记与定容拒绝，并发出确定性通过/失败 marker

#### Scenario: 默认启动不依赖多路复用能力

- **WHEN** 多路复用验证 switch 关闭
- **THEN** 默认启动、fd/VFS、pipe、tty、socket 与 userland baseline MUST 维持既有行为并正常进入 shell

#### Scenario: 验证不可用时记录跳过

- **WHEN** QEMU、串口捕获或 x86_64-elf 工具链等验证依赖不可用
- **THEN** 验证记录 MUST 区分已通过、无法运行（含原因与剩余风险）与历史诊断，MUST NOT 声称运行成功
