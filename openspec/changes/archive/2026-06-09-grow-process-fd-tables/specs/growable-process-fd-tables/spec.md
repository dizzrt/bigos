## ADDED Requirements

### Requirement: 进程注册结构可增长可回收

BigOS SHALL 用可增长、可回收的内核进程注册结构承载活跃、zombie 与 reap-pending 进程，移除编译期固定的 `MAX_PROCESSES` 硬上限，改由可配置软上限约束。该结构 SHALL 单核安全，且 SHALL NOT 隐含 SMP、命名空间、进程组或会话语义。

#### Scenario: 超过旧固定上限仍能创建进程

- **WHEN** 已注册的活跃进程数超过旧的固定上限（16）且仍低于软上限
- **THEN** BigOS MUST 仍能注册新的进程并为其分配稳定且不与现存活跃/zombie 进程冲突的 PID
- **AND** 该进程 MUST 可通过 PID 查找，直至被完全 reap

#### Scenario: 达到软上限确定性降级

- **WHEN** 进程创建会使注册进程数超过配置的软上限
- **THEN** BigOS MUST 确定性地失败创建，不发布部分初始化的进程，且 MUST NOT panic
- **AND** 失败尝试中已分配的地址空间根、用户页、内核栈或加载缓冲 MUST 被释放或标记为可安全释放

#### Scenario: 回收后槽位与 PID 可复用

- **WHEN** 一个进程被完全 reap 并从注册结构中移除
- **THEN** 其占用的注册槽位 MUST 可被后续新进程复用
- **AND** 其 PID MUST 在状态被消费/完全 reap 后才可被复用，复用 MUST NOT 错乱父子链或 reap 链

### Requirement: 进程对象堆分配与回收

BigOS SHALL 用内核堆分配（`kmalloc`/`free`）承载 `Process` 对象，替代 `static Process` 单例，并在进程被完全 reap 后归还对象内存。进程对象的分配与释放 SHALL 只发生在允许阻塞的非 IRQ 进程创建/回收上下文。

#### Scenario: 进程对象分配成功

- **WHEN** 创建新进程且内核堆有容量
- **THEN** BigOS MUST 从内核堆分配一个清零的 `Process` 对象并用于该进程
- **AND** 该对象 MUST 不与其他活跃进程共享存储

#### Scenario: 进程对象分配失败确定性降级

- **WHEN** `Process` 对象的内核堆分配失败
- **THEN** BigOS MUST 确定性地失败进程创建并返回错误，MUST NOT panic
- **AND** MUST NOT 发布引用空对象的进程

#### Scenario: reap 后释放对象内存

- **WHEN** 一个进程到达安全 reaper 边界且资源已回收、已从注册结构 unpublish
- **THEN** BigOS MUST 在该对象不再被 `current`、reap 链或父子链引用后释放其堆内存
- **AND** MUST NOT 在释放后再访问该对象状态

### Requirement: 每进程 fd 表可增长可回收

BigOS SHALL 用可增长结构承载每进程 fd 表，移除编译期固定的 `MAX_FDS` 硬上限，改由可配置软上限约束，并随进程对象生命周期分配与回收 fd 存储。fd 号分配 SHALL 仍优先返回当前表内最低可用下标。

#### Scenario: 超过旧固定上限仍能分配 fd

- **WHEN** 一个进程已打开的 fd 数超过旧的固定上限（16）且仍低于软上限
- **THEN** BigOS MUST 仍能分配稳定的最低可用 fd 并绑定到打开的文件对象

#### Scenario: fd 软上限或增长失败确定性降级

- **WHEN** fd 分配会超过配置的软上限，或 fd 表增长所需的内核堆分配失败
- **THEN** BigOS MUST 确定性地返回 `EMFILE` 并释放任何未发布的打开文件对象，MUST NOT panic

#### Scenario: fd 存储随进程回收释放

- **WHEN** 一个进程被完全 reap
- **THEN** BigOS MUST 关闭所有剩余 fd 各一次并释放该进程的 fd 存储
- **AND** MUST NOT 从不安全的 syscall、异常、IRQ 或活动栈 teardown 路径释放活动文件状态

### Requirement: 可增长进程与 fd 表验证可复现

BigOS SHALL 提供默认关闭的验证开关，独立验证可增长进程/fd 表的能力，并发射固定的 COM1/VGA marker。该验证 SHALL NOT 改变默认 boot 行为。

#### Scenario: 默认关闭

- **WHEN** 不启用 `growable_tables_smoke` 开关进行常规构建与 boot
- **THEN** 可增长表的验证路径 MUST NOT 运行，默认 boot 行为 MUST 不受影响

#### Scenario: 启用开关运行验证

- **WHEN** 启用 `growable_tables_smoke`（`BIGOS_GROWABLE_TABLES_SMOKE`）构建并在 QEMU headless 运行
- **THEN** 验证 MUST 覆盖「超过旧 16 上限创建进程/fd」「回收后槽位与 PID 复用」「分配失败确定性降级」路径
- **AND** MUST 在 COM1/VGA 发射 `BIGOS_GROWABLE_TABLES_PASSED` 或 `BIGOS_GROWABLE_TABLES_FAILED`
