## ADDED Requirements

### Requirement: 运行时 smoke 矩阵覆盖默认 init 行为断言

BigOS SHALL 在 Stage 9 运行时 smoke 矩阵中新增一个**默认构建**（不开启任何 smoke
开关）的用例，断言 normal boot 默认进入用户态 init 的行为，并以此启动「行为断言测试」
轨道——验证逐步从源码字符串契约转向基于串口 marker 与用户态二进制输出的行为断言。

#### Scenario: 矩阵包含默认 init 用例

- **WHEN** 开发者在 Stage 14.5 之后查看运行时 smoke 矩阵
- **THEN** 矩阵 MUST 包含一个不依赖任何 smoke 开关的默认构建用例
- **AND** 该用例 MUST 断言默认构建发出 `BIGOS_INIT_ENTER` 与 `BIGOS_INIT_EXIT` 串口 marker
- **AND** 该用例 MUST 列出（空的）所需 smoke 开关、首选 QEMU headless 路径、期望 marker、
  用例专属超时以及生成的日志/artifact 路径

#### Scenario: 默认 init 用例采用行为断言

- **WHEN** 运行器执行默认 init 用例
- **THEN** 它 MUST 以内核 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT` 串口 marker 作为通过
  判据，而非断言内核 C++ 源码字符串
- **AND** 缺失期望 marker MUST 被判定为失败，而不能被重新解读为通过
- **AND** init 二进制自身的 stdout 输出断言 MUST 留待后续阶段引入，本 change 不要求

#### Scenario: 默认 init 用例不改变其他 smoke 默认值

- **WHEN** 默认 init 用例加入矩阵
- **THEN** 既有 memory、timer、scheduler、syscall、filesystem、blocking、user_program、
  user_elf 等用例的 smoke 默认值 MUST 保持不变
- **AND** `user_program_smoke` / `user_elf_smoke` 用例 MUST 仍作为额外验证路径保留在矩阵中
