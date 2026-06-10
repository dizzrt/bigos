## ADDED Requirements

### Requirement: 进程携带信号状态字段

BigOS SHALL 为每个进程维护信号 pending 位图、阻塞掩码与每信号处置表字段（追加字段，不重排既有布局），并定义其在 init/ELF 创建路径下的初始化规则。

#### Scenario: 进程创建初始化信号状态

- **WHEN** 进程通过 init 或非 fork ELF 创建路径产生
- **THEN** 其每信号处置 MUST 初始化为默认动作，阻塞掩码 MUST 为空，pending 位图 MUST 为空
- **AND** 这些字段 MUST 为定长内联存储，初始化 MUST NOT 引入新的分配失败路径

### Requirement: 信号默认终止复用退出生命周期

BigOS SHALL 让信号的默认 Terminate 动作复用现有 exit/fault-to-reaper teardown 与退出状态语义，不引入独立的进程回收路径。

#### Scenario: 默认终止经 reaper 回收

- **WHEN** 一个进程因默认 Terminate 信号（或 `SIGKILL`）被终止
- **THEN** BigOS MUST 通过现有 exit/fault-to-reaper 生命周期回收其地址空间与进程资源
- **AND** MUST 把信号号编码进退出/fault 状态供父进程 `wait` 或诊断观察
- **AND** MUST NOT 改变既有 zombie/reaper、`wait_status_consumed`、`parent_waiting` 语义

### Requirement: 子进程退出向父进程投递 SIGCHLD

BigOS SHALL 在子进程进入 zombie（退出或被信号终止）时，向其父进程的 pending 位图投递 `SIGCHLD`，默认动作为 Ignore，且不改变现有 `wait`/reaper 判定。

#### Scenario: 子进程退出置位父进程 SIGCHLD

- **WHEN** 一个子进程退出或被信号终止进入 zombie
- **THEN** BigOS MUST 在其父进程 pending 位图置位 `SIGCHLD`
- **AND** 该置位 MUST NOT 分配内存，MUST NOT 改变现有 `wait` 唤醒与 reaper 回收行为
- **AND** 父进程未注册 `SIGCHLD` handler 时该信号 MUST 按默认 Ignore 处理而不改变父进程行为
