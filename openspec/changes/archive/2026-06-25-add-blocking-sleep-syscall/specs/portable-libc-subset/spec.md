## ADDED Requirements

### Requirement: bounded libc exposes blocking sleep wrappers
BigOS portable libc subset SHALL expose bounded blocking sleep wrappers backed by the BigOS sleep syscall. The wrappers MUST compile and link in the freestanding user program environment, use the repository's syscall/errno translation convention, and avoid implying complete POSIX time, timer, signal-interruptible sleep, or hosted libc behavior.

#### Scenario: BigOS-specific millisecond sleep wrapper
- **WHEN** a user program includes the supported public libc headers and calls `bigos_sleep_ms(milliseconds)`
- **THEN** libc MUST invoke the blocking sleep syscall with the requested millisecond duration
- **AND** it MUST return 0 on success or -1 with `errno` set when the kernel returns a negative errno

#### Scenario: 受限 sleep seconds wrapper
- **WHEN** a user program calls `sleep(seconds)` through the bounded libc subset
- **THEN** libc MUST implement it on top of the BigOS blocking sleep syscall using seconds-to-milliseconds conversion with overflow handling
- **AND** when the underlying syscall fails or a future early-return path becomes observable, libc MUST estimate elapsed whole seconds from monotonic tick readings and return the remaining seconds instead of always returning the original input
- **AND** it MUST document that this wrapper is a bounded POSIX-like compatibility surface, not complete POSIX `sleep(3)` behavior

#### Scenario: signal interruption 语义不被声明
- **WHEN** headers, docs, or examples describe `sleep(seconds)` or `bigos_sleep_ms(milliseconds)`
- **THEN** they MUST NOT claim support for signal interruption, kernel-written remaining time after signal delivery, `nanosleep`, `clock_nanosleep`, `usleep`, `alarm`, timerfd, or high-resolution timers
- **AND** unsupported behavior MUST require a later explicit specification before becoming public supported API

#### Scenario: freestanding header 可构建
- **WHEN** representative static user programs include `unistd.h` or `time.h` and use only the documented bounded sleep declarations
- **THEN** those declarations MUST resolve through BigOS user libc headers without host libc headers
- **AND** the program MUST statically link through the existing user program build path
