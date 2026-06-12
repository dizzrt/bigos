## ADDED Requirements

### Requirement: 运行时文件系统行为验证可复现

BigOS SHALL 为有界运行时文件系统可用性提供分层验证：OpenSpec/文档一致性、源码或构建检查、用户态 C 程序行为检查、shell 重定向检查，以及环境可用时的 QEMU/Bochs 运行时 smoke。验证 MUST 记录工具链、emulator、ROM/display、磁盘镜像和 `uv` 可用性；环境依赖不可满足时 MUST 记录跳过原因、替代检查和残余风险。

#### Scenario: 用户程序验证覆盖文件操作
- **WHEN** 运行运行时文件系统行为验证
- **THEN** 验证 MUST 覆盖创建、写入、seek、读回、fsync、mkdir、最小目录枚举、unlink、只读后端拒写和容量/权限/非法路径失败中的代表性路径
- **AND** 结果 MUST 能通过用户程序输出、退出状态、串口日志或确定性测试报告判断

#### Scenario: 优先复用 userland smoke
- **WHEN** 增加运行时文件系统行为验证
- **THEN** 验证 MUST 优先复用或扩展现有 userland smoke 的打包、启动和可观察输出路径
- **AND** 只有在复用导致用例耦合过大时才拆出小型专用用户程序

#### Scenario: shell 验证覆盖重定向
- **WHEN** 运行 shell 或 userland 组合验证
- **THEN** 验证 MUST 覆盖至少一个输出重定向到 `/rw` 文件并读回的路径
- **AND** 失败重定向 MUST 被观察为确定性错误而不是 shell 崩溃

### Requirement: 验证记录区分通过、跳过和残余风险

BigOS SHALL 在运行时文件系统可用性验证记录中区分已通过检查、因环境不可用跳过的检查、历史诊断、当前变更引入的问题和残余风险。涉及 Python 辅助脚本时 MUST 通过 `uv run ...` 执行；`uv` 不可用时 MUST 明确记录阻塞而不是静默使用系统 Python。

#### Scenario: emulator 不可用时记录跳过
- **WHEN** QEMU、Bochs、cross-toolchain、ROM/display、磁盘镜像或串口 oracle 不可用
- **THEN** 对应运行时 smoke MAY 被跳过
- **AND** 验证记录 MUST 标明缺失条件、已执行替代检查和剩余 bootability 或行为风险

#### Scenario: Python 辅助验证遵守 uv 约定
- **WHEN** 运行 Python helper、pytest、ruff 或 pyright 相关验证
- **THEN** 命令 MUST 使用 `uv run ...`
- **AND** 若 `uv` 不可用，验证记录 MUST 明确该 blocker
