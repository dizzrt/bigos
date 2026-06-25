## ADDED Requirements

### Requirement: libc 暴露 BigOS terminal mode wrapper

BigOS 用户态 libc SHALL expose minimal BigOS-specific terminal mode wrappers and constants for simple static C programs. The wrappers MUST be named `bigos_tcgetmode` and `bigos_tcsetmode`, MUST call the bounded terminal mode syscalls, translate negative kernel errno to user `errno`, and avoid claiming complete POSIX `termios`.

#### Scenario: 查询 wrapper 返回 mode

- **WHEN** a user program calls `bigos_tcgetmode` with a valid output object
- **THEN** libc MUST invoke the terminal-mode query syscall through the fixed `int 0x80` ABI
- **AND** on success the program MUST observe the current canonical or raw mode value

#### Scenario: 设置 wrapper 切换 raw/canonical

- **WHEN** a user program calls `bigos_tcsetmode` with a valid canonical or raw mode value
- **THEN** libc MUST invoke the terminal-mode set syscall through the fixed `int 0x80` ABI
- **AND** success or failure MUST follow the existing wrapper return and `errno` translation convention

#### Scenario: headers 不声明完整 termios

- **WHEN** userland headers expose terminal mode constants or wrapper prototypes
- **THEN** they MUST identify the interface as a BigOS bounded terminal mode subset
- **AND** they MUST NOT declare POSIX-style `tcgetattr` / `tcsetattr`, unsupported POSIX `termios` fields, baud-rate APIs, `VMIN/VTIME`, pseudo-terminal APIs, or complete terminal database behavior

#### Scenario: 简单程序可恢复 canonical

- **WHEN** a simple static user program enters raw mode and later calls `bigos_tcsetmode` to restore canonical mode
- **THEN** libc MUST provide the declarations and implementation needed for that restore path
- **AND** the program MUST NOT need hosted libc, dynamic linking, or direct syscall assembly beyond the wrapper
