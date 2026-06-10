## ADDED Requirements

### Requirement: 内核在启动时建立一次性墙钟基准

BigOS SHALL 在启动期读取一次 CMOS RTC 的 UTC 时间，转换为 Unix epoch 秒作为墙钟基准，并记录此刻的单调 tick，使墙钟时间可由现有单调 tick 推进。

#### Scenario: 启动建立墙钟基准

- **WHEN** 内核在 timer 单调 tick 可用之后初始化墙钟模块
- **THEN** 它 MUST 读取 CMOS RTC（经端口 0x70/0x71）一次，得到年/月/日/时/分/秒（UTC）
- **AND** 它 MUST 把该时间换算为 Unix epoch 秒并记录为 `boot_unix_time`，同时记录此刻的 `boot_tick = timer::ticks()`
- **AND** 启动期之后 MUST NOT 进行周期性 RTC 轮询或 RTC IRQ 注册

#### Scenario: RTC 读取处理 update-in-progress 与编码

- **WHEN** 墙钟模块读取 RTC
- **THEN** 它 MUST 在读取时间字段前轮询状态寄存器 A 的 UIP 位（带有界上限）以避免读到半更新值
- **AND** 它 MUST 根据状态寄存器 B 判断 BCD 与 12/24 小时制并相应归一化字段

### Requirement: 内核提供单调推进的当前墙钟查询

BigOS SHALL 提供只读的当前墙钟查询 API，基于墙钟基准与单调 tick 推进，返回单调不减的 Unix epoch 秒，且查询路径不访问硬件、不分配、不阻塞。

#### Scenario: 当前墙钟随 tick 推进

- **WHEN** 内核或用户态查询当前墙钟时间
- **THEN** 结果 MUST 等于 `boot_unix_time + (timer::ticks() - boot_tick) / TIMER_HZ`
- **AND** 在单调 tick 推进时，连续两次查询的返回值 MUST 单调不减
- **AND** 查询路径 MUST NOT 访问 RTC 硬件、分配内存或阻塞

#### Scenario: 启动基准也可单独查询

- **WHEN** 内核查询墙钟基准
- **THEN** 它 MUST 返回启动时建立的 `boot_unix_time`

### Requirement: RTC 无效时墙钟确定性退化

BigOS SHALL 在 RTC 读取失败或字段越界时确定性退化到固定基准并发射诊断 marker，绝不 panic、阻塞或在墙钟路径中分配内存。

#### Scenario: RTC 字段越界退化

- **WHEN** RTC 的 UIP 轮询超过上限，或读出的月/日/时/分/秒字段超出合法范围
- **THEN** 墙钟模块 MUST 把 `boot_unix_time` 设为固定基准（epoch 0）并仍记录 `boot_tick`
- **AND** 它 MUST 发射固定诊断 marker（如 `BIGOS_RTC_INVALID`）到 COM1/VGA
- **AND** 它 MUST NOT panic、阻塞或在该路径分配内存
- **AND** 退化后当前墙钟查询 MUST 仍单调可用（基准为 0 之上的 tick 推进）

### Requirement: 墙钟能力经默认关闭开关验证

BigOS SHALL 通过默认关闭的验证开关与源码/行为断言验证墙钟能力，且不改变默认启动 marker 与既有 smoke 矩阵。

#### Scenario: 墙钟 smoke 发射有界 marker

- **WHEN** 启用 `time_identity_smoke`（`BIGOS_TIME_IDENTITY_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 观察到墙钟在 RTC 基准之上随 tick 单调推进的有界判定 marker（如 `BIGOS_TIME_IDENTITY_PASSED`/`BIGOS_TIME_IDENTITY_FAILED`）
- **AND** 该开关 MUST 默认关闭，默认启动 marker 与既有 smoke 矩阵 MUST 保持不变

#### Scenario: 模拟器不可用时记录跳过

- **WHEN** 模拟器、ROM、交叉工具链或串口 oracle 不可用
- **THEN** 验证 MUST 记录缺失依赖、已通过的源码/构建检查，以及墙钟路径的剩余风险
