# fd-readiness-model 规格增量

本规格定义内核内统一的 fd 就绪（readiness）查询模型。所有 requirement 均以内核内可观测行为或默认关闭 smoke 的 COM1 标记表达，不引入用户态多路复用 syscall，也不暗示完整 POSIX poll 语义。

## ADDED Requirements

### Requirement: 统一 fd 就绪查询入口

内核 SHALL 在 VFS 层提供一个统一的就绪查询入口，对给定的 `vfs::File` 返回一组就绪位标志，至少覆盖可读、可写、错误三类状态。该入口 MUST 为纯只读快照：不得出队数据、不得阻塞调用线程、不得修改文件或后端的打开状态。

就绪位标志 MUST 定义为稳定的内核内部常量，可按位或组合；本变更不得把这些标志暴露为用户可见 syscall ABI。

#### Scenario: 查询入口返回就绪位标志

- **WHEN** 内核代码对一个有效的 `vfs::File` 调用统一就绪查询入口
- **THEN** 返回值是可读、可写、错误三类就绪位的按位或，且本次调用不改变该描述符的数据、偏移或打开状态

#### Scenario: 未实现就绪操作的文件返回确定性默认

- **WHEN** 被查询的 `vfs::File` 所属后端未提供就绪操作（例如常规文件）
- **THEN** 入口返回确定性默认：可读位与可写位置位、错误位清零，而不是失败或未定义结果

### Requirement: 就绪查询通过文件操作表分发且不重排既有槽位

就绪查询 SHALL 通过在 `vfs::FileOperations` 末尾追加的可选操作进行分发。新增操作 MUST 追加在既有 `read/close/write/lseek/truncate/readdir` 槽位之后，不得重排或改变既有槽位含义；既有后端在不提供该操作时 MUST 仍可正常工作并获得确定性默认就绪结果。

#### Scenario: 既有后端无需改动即可被查询

- **WHEN** 一个未实现就绪操作的既有文件后端被统一就绪查询入口查询
- **THEN** 入口检测到该操作为空并返回确定性默认就绪结果，既有后端的 `read/write/lseek` 等行为不受影响

### Requirement: pipe 描述符就绪语义与阻塞行为一致

对 pipe 描述符，就绪查询 SHALL 复用既有就绪判断，使查询结果与阻塞读写行为一致：当且仅当对应的阻塞读不会再阻塞时报告可读，当且仅当对应的阻塞写不会再阻塞时报告可写。

- 缓冲区有数据，或写端已关闭（EOF 可读），MUST 报告可读。
- 缓冲区有空间，或读端已关闭，MUST 报告可写。
- 读端已关闭时，写方向 MUST 报告错误位（broken pipe 倾向）。

#### Scenario: 有数据的 pipe 报告可读

- **WHEN** 一个 pipe 的读端被查询且缓冲区中存在未读数据
- **THEN** 查询结果包含可读位，且随后对该读端的阻塞读立即返回数据而不阻塞

#### Scenario: 写端关闭的 pipe 读端报告可读 EOF

- **WHEN** 一个 pipe 的写端已关闭且缓冲区为空，查询其读端
- **THEN** 查询结果包含可读位，表示读取将得到 EOF 而非阻塞

#### Scenario: 读端关闭的 pipe 写端报告错误

- **WHEN** 一个 pipe 的读端已关闭，查询其写端
- **THEN** 查询结果包含错误位，表示后续写入将失败（broken pipe）

### Requirement: socket 接收路径具备等待队列并表达就绪

UDP socket 的接收端点 SHALL 具备一个调度等待队列。协议接收投递路径在把 datagram 放入接收队列之后 MUST 唤醒该等待队列，且唤醒 MUST 是分配无关、可在中断/投递上下文安全调用的（复用既有 `wake_one`/`wake_all` 约定）。

对 socket 描述符，就绪查询 SHALL 把“接收队列非空”表达为可读、把“端点处于可发送的有效状态”表达为可写、把“端点失活或不可用”表达为错误位。

#### Scenario: 收到数据后唤醒等待并报告可读

- **WHEN** 协议接收路径把一个 datagram 投递进某 UDP 端点的接收队列
- **THEN** 该端点的等待队列被唤醒，且随后对该 socket 的就绪查询包含可读位

#### Scenario: 空接收队列的 socket 不报告可读

- **WHEN** 一个已绑定但接收队列为空的 socket 被查询
- **THEN** 查询结果不包含可读位，可写位按端点可发送状态置位

### Requirement: 终端描述符就绪经 TTY 文件操作表表达

终端（tty）描述符已表达为指向 `TTY_OPS` 的 `vfs::File`。终端就绪查询 SHALL 由 `TTY_OPS` 的就绪操作实现，复用既有输入可用判断：当 tty 输入环中存在可消费输入（或存在挂起的转义序列字节）时 MUST 报告可读；终端写出方向 MUST 报告可写；终端输入路径不产生错误位。该操作 MUST 仅做只读查询，不消费输入记录、不改动输入环游标。

由于所有终端描述符（包括标准输入以外的终端 fd，如 fd 1/2 以及 `dup` 得到的副本）共享同一 `TTY_OPS` 句柄，对它们的就绪查询 MUST 返回一致结果，且不依赖任何裸 fd 特例。

#### Scenario: 有输入的终端报告可读

- **WHEN** tty 输入环中存在尚未消费的输入记录，查询任一终端描述符就绪
- **THEN** 查询结果包含可读位，且本次查询不出队任何输入记录

#### Scenario: 无输入的终端不报告可读

- **WHEN** tty 输入环为空且无挂起转义序列字节，查询终端描述符就绪
- **THEN** 查询结果不包含可读位，可写位置位

#### Scenario: 标准输入以外的终端 fd 就绪一致

- **WHEN** 对共享同一 `TTY_OPS` 句柄的另一终端 fd（例如 `dup(0)` 得到的副本）查询就绪
- **THEN** 其就绪结果与标准输入终端 fd 一致，且不依赖裸 fd 特例

### Requirement: 就绪模型的默认关闭运行期验证

就绪模型 SHALL 提供一个默认关闭的运行期 smoke，通过 xmake 开关映射到 `BIGOS_*` 宏，在内核启动的 smoke 阶段对 pipe、socket、tty 三类描述符断言就绪位与既有阻塞行为一致，并在 COM1 输出确定性 PASSED/FAILED 标记。该 smoke MUST 默认关闭，不改变默认启动行为。

#### Scenario: smoke 验证通过时输出 PASSED 标记

- **WHEN** 启用该 smoke 开关构建并在 QEMU headless 路径运行，三类描述符的就绪位均与对应阻塞行为一致
- **THEN** 内核在 COM1 输出该 smoke 的 PASSED 标记

#### Scenario: smoke 默认关闭不影响默认启动

- **WHEN** 不启用该 smoke 开关构建并正常启动
- **THEN** 默认启动行为与既有基线一致，不输出该 smoke 的标记
