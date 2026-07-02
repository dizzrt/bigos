## MODIFIED Requirements

### Requirement: 管道 EOF 与 EPIPE 语义

BigOS SHALL 在所有写端关闭后令读端读到 EOF（读返回 0），在所有读端关闭后令写端写返回确定性 `-EPIPE` 并向写者投递 `SIGPIPE`（默认动作终止）。端引用计数 MUST 精确管理，最后一个端关闭时 MUST 确定性唤醒对端。`SIGPIPE` 的投递 MUST 复用既有信号投递路径并与 stream socket 的 broken-pipe 语义一致；当写者进程对 `SIGPIPE` 设置 `SIG_IGN` 时写 MUST 仅返回 `-EPIPE` 而不终止。

#### Scenario: 写端全关读到 EOF

- **WHEN** 管道所有写端已关闭且缓冲已被读空
- **THEN** 读者的读 MUST 返回 0（EOF），MUST NOT 阻塞

#### Scenario: 读端全关写返回 EPIPE 并投递 SIGPIPE

- **WHEN** 管道所有读端已关闭，写者尝试写入
- **THEN** BigOS MUST 返回确定性 `-EPIPE` 并向写者投递 `SIGPIPE`（默认动作终止）
- **AND** 当写者进程对 `SIGPIPE` 设置 `SIG_IGN` 时 MUST 仅返回 `-EPIPE` 而不终止进程

### Requirement: 管道能力验证可复现

BigOS SHALL 通过默认关闭的运行时 smoke 与源码/行为断言验证管道与 fd 复制。验证 MUST 记录工具链与模拟器可用性、串口 marker、跳过的用例与残余风险，且默认启动 marker 与既有 smoke 矩阵 MUST 保持不变。管道 smoke MUST 覆盖读端全关写返回 `-EPIPE` 且默认投递 `SIGPIPE` 终止（未忽略时）与 `SIG_IGN` 下仅 `-EPIPE` 的行为。

#### Scenario: pipe smoke 发射有界 marker

- **WHEN** 启用 `pipe_smoke`（`BIGOS_PIPE_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「跨进程写读 FIFO 一致」「读空阻塞 + 写入唤醒」「写端全关读 EOF」「读端全关写 EPIPE 且投递 SIGPIPE」「`SIG_IGN` 下写仅 EPIPE 不终止」「`dup`/`dup2` 共享 offset」并发射 `BIGOS_PIPE_PASSED`/`BIGOS_PIPE_FAILED`
- **AND** 该开关 MUST 默认关闭；QEMU/Bochs 或交叉工具链不可用时 MUST 记录为跳过验证而非声称通过
