## ADDED Requirements

### Requirement: 默认文本控制台承载交互式用户态 I/O
BigOS SHALL 将当前运行时文本控制台作为默认有界用户态 shell 的可见 I/O 承载路径，使键盘输入能经 TTY 输入缓冲进入用户态 stdin，并使用户态 stdout/stderr 能经控制台输出路径显示。

#### Scenario: 默认 shell 可以从控制台读取键盘输入
- **WHEN** normal boot 进入默认 resident init 并启动 `/bin/sh`
- **THEN** `/bin/sh` 的标准输入 MUST 能从默认 TTY/console 输入路径读取键盘产生的有界字符流
- **AND** 该路径 MUST 继续使用现有 IRQ-safe keyboard producer 和非中断 TTY consumer 边界

#### Scenario: 用户态输出显示到文本控制台
- **WHEN** 默认 shell 或其启动的简单用户程序向 stdout 或 stderr 写入有界文本
- **THEN** BigOS MUST 将该文本显示到运行时文本控制台
- **AND** 普通用户态输出 MUST NOT 要求直接调用早期 diagnostic-only 输出 API

### Requirement: 控制台输入回显保持 IRQ-safe 边界
BigOS SHALL 在非中断消费路径中产生普通输入回显，使用户键入的 printable 字符和基本行编辑反馈可见，同时保持 keyboard IRQ1 handler 有界、非阻塞且不直接执行普通控制台输出。

#### Scenario: Printable input is echoed outside IRQ context
- **WHEN** keyboard IRQ1 将 printable 字符放入 TTY 输入缓冲
- **THEN** BigOS MUST NOT 从 keyboard IRQ1 handler 直接写 VGA 文本输出或串口格式化输出作为普通回显
- **AND** 普通回显 MUST 由后续非中断 TTY/console/userland 消费路径产生

#### Scenario: Basic line feedback remains bounded
- **WHEN** 用户在默认 shell 输入路径中键入 printable 字符、换行或 backspace
- **THEN** BigOS MUST 以有界方式显示对应输入反馈或行结束效果
- **AND** 该行为 MUST NOT 依赖动态分配、完整 terminal escape 支持、termios 或 job-control 状态
