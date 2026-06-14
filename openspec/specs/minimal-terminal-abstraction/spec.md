## Purpose

Define the minimal default console terminal abstraction boundary for BigOS.
This capability describes bounded ownership of the current single runtime text
console's input, output, and state without introducing full POSIX terminal
semantics.

## Requirements

### Requirement: 默认控制台终端具有最小抽象边界

BigOS SHALL 定义一个默认控制台终端抽象，用于描述当前单一运行时文本控制台的有界输入、输出和状态归属。该抽象 MUST 建立在现有 x86_64 Legacy BIOS baseline、TTY 输入缓冲、console output API 和用户态 fd/syscall 路径之上，MUST NOT 引入多终端设备模型、伪终端、session、job control、terminal process group 或完整 POSIX terminal 支持。

#### Scenario: 默认终端绑定到现有控制台路径

- **WHEN** normal boot 进入默认 resident init 并启动 `/bin/sh`
- **THEN** BigOS MUST 将 shell 的标准输入、标准输出和标准错误连接到默认控制台终端语义
- **AND** 该语义 MUST 通过现有 TTY/console/fd/syscall 路径实现，而不是要求 shell 或用户程序直接访问 VGA、COM1 或 early diagnostic-only API

#### Scenario: 终端抽象不扩大 backend 范围

- **WHEN** 默认控制台终端能力被实现、记录或验证
- **THEN** it MUST NOT require UEFI runtime parity, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, a second ISA backend, dynamic linking, or a complete hosted runtime
- **AND** it MUST NOT change kernel link addresses, page-table layout, direct-map assumptions, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or boot handoff ABI

### Requirement: 终端输入归属保持 IRQ producer 与非中断 consumer 分层

BigOS SHALL keep terminal input ownership split between an IRQ-safe keyboard producer and non-interrupt terminal consumers. Keyboard IRQ1 MAY enqueue bounded input characters or terminal input events and MAY wake waiters through a bounded IRQ-safe path, but ordinary echo, line discipline decisions, EOF/interrupt interpretation, and shell policy MUST occur outside IRQ context.

#### Scenario: IRQ 只生产有界输入

- **WHEN** keyboard IRQ1 receives a supported scancode or control-key combination
- **THEN** BigOS MUST translate or classify it into bounded input data or a bounded terminal input event
- **AND** the IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, call hosted runtime APIs, perform ordinary console echo, or directly execute shell policy

#### Scenario: 非中断路径消费终端输入

- **WHEN** shell or another ordinary non-interrupt consumer reads from the default terminal stdin path
- **THEN** BigOS MUST deliver input in deterministic FIFO or documented event order within fixed bounds
- **AND** empty input MUST either follow the existing non-blocking behavior or block only through the existing non-interrupt wait path

### Requirement: 终端控制字符语义保持有界

BigOS SHALL define a minimal bounded set of terminal control-character semantics needed by the default shell and simple user programs. The set MUST include deterministic handling for line end, backspace/delete-like editing feedback, EOF-like input, interrupt-like input, and unsupported control bytes. These semantics MUST NOT imply termios, canonical mode completeness, signal delivery to process groups, or complete POSIX terminal behavior.

#### Scenario: 行结束和退格可观察

- **WHEN** a user types printable characters, newline/carriage return, or backspace-like input on the default terminal path
- **THEN** the shell or terminal consumer MUST observe deterministic line input and bounded feedback behavior
- **AND** the behavior MUST NOT require terminal escape parsing, dynamic allocation, job-control state, or session state

#### Scenario: EOF-like input is deterministic

- **WHEN** the default terminal receives the configured EOF-like control input
- **THEN** BigOS MUST expose a deterministic EOF-like result to the non-interrupt reader or shell line-input path
- **AND** that result MUST NOT imply complete POSIX canonical-mode semantics

#### Scenario: Interrupt-like input is bounded

- **WHEN** the default terminal receives the configured interrupt-like control input
- **THEN** BigOS MUST expose a deterministic bounded result such as a terminal event, shell-visible cancellation, or documented no-op
- **AND** the behavior MUST NOT require terminal process groups, job control, sessions, or full POSIX signal terminal semantics

### Requirement: 终端输出归属通过普通 fd/syscall 路径

BigOS SHALL route ordinary shell prompt text, command output, shell errors, and simple user-program stdout/stderr through the existing userland fd/syscall path to the default console terminal output sink. Early diagnostics, panic paths, page-fault diagnostics, and fixed smoke markers MAY continue to use direct VGA/COM1 diagnostic APIs independently of terminal initialization.

#### Scenario: 普通输出经 fd 到控制台

- **WHEN** `/bin/sh` or a simple user program writes bounded text to stdout or stderr connected to the default terminal
- **THEN** BigOS MUST make that text visible through the runtime text console output path
- **AND** the program MUST NOT require direct access to VGA memory, COM1 port IO, or kernel diagnostic-only output APIs

#### Scenario: Early diagnostics remain independent

- **WHEN** a panic, early fault, or runtime smoke marker is emitted before or outside terminal readiness
- **THEN** BigOS MAY continue using existing direct diagnostic output paths
- **AND** those diagnostic paths MUST NOT depend on terminal input state, shell progress, or userland fd availability
