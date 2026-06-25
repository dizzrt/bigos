## Purpose

Define the minimal BigOS TTY and console input/output foundation: bounded PS/2 keyboard decoding, IRQ-safe input handoff, fixed-capacity TTY buffering, a minimal console output API, conservative initialization ordering, and reproducible validation expectations.
## Requirements
### Requirement: Keyboard input is decoded into bounded TTY events

BigOS SHALL convert supported PS/2 set-1 keyboard scancodes into bounded TTY input events or ASCII characters without requiring scheduler, heap allocation, filesystem, user mode, or hosted runtime services.

#### Scenario: Supported scancode becomes ASCII input

- **WHEN** keyboard IRQ1 delivers a supported PS/2 set-1 make scancode for a printable US-layout key
- **THEN** BigOS converts it to the corresponding ASCII character using the current modifier state
- **AND** BigOS enqueues the character into the TTY input buffer

#### Scenario: Modifier state affects key translation

- **WHEN** Shift make and break scancodes are observed around an alphabetic or symbol key
- **THEN** BigOS updates modifier state and applies it to subsequent supported key translations
- **AND** modifier state updates MUST NOT enqueue printable input by themselves

#### Scenario: Unsupported scancode is safe

- **WHEN** keyboard IRQ1 delivers an unsupported, extended, or unmapped scancode
- **THEN** BigOS drops or records the scancode without corrupting input buffer state
- **AND** BigOS MUST NOT panic, allocate memory, block, or call hosted runtime APIs

### Requirement: Keyboard ISR is IRQ-context safe

BigOS SHALL keep the keyboard IRQ1 handler bounded and IRQ-context safe while handing input to the TTY layer.

#### Scenario: ISR performs bounded input handoff

- **WHEN** the keyboard IRQ1 handler runs
- **THEN** it reads one byte from PS/2 data port `0x60`
- **AND** it performs only bounded modifier/key translation and fixed-capacity input enqueue operations
- **AND** it returns to the IRQ dispatch layer for EOI

#### Scenario: ISR avoids complex runtime dependencies

- **WHEN** the keyboard IRQ1 handler runs
- **THEN** it MUST NOT allocate or free memory
- **AND** it MUST NOT block, sleep, poll for line input, call `mdelay()`, use filesystem services, call `kprintf`, or depend on scheduler, process, syscall, user-mode, or TTY consumer progress

#### Scenario: ISR does not directly write console output

- **WHEN** keyboard input is received in IRQ context
- **THEN** the ISR MUST NOT directly write VGA text output or serial formatted output for normal input handling
- **AND** any echo or smoke marker is emitted from a non-interrupt consumer path or from a separately bounded validation path

### Requirement: TTY input buffer is fixed-capacity and non-blocking

BigOS SHALL provide a fixed-capacity input buffer that supports IRQ-context producers and non-interrupt consumers under the current single-core early-kernel model.

#### Scenario: IRQ producer enqueues without allocation

- **WHEN** translated keyboard input is available in IRQ context
- **THEN** BigOS enqueues it into statically owned or initialization-owned fixed-capacity storage
- **AND** enqueue MUST NOT allocate memory or wait for space

#### Scenario: Full buffer is handled deterministically

- **WHEN** translated input arrives and the TTY input buffer is full
- **THEN** BigOS drops the new input or records an overflow counter deterministically
- **AND** BigOS MUST NOT overwrite unread input unless the overflow policy is explicitly documented

#### Scenario: Non-interrupt consumer reads available input

- **WHEN** non-interrupt kernel code polls or drains the TTY input buffer
- **THEN** BigOS returns available characters in FIFO order
- **AND** an empty buffer returns a non-blocking empty result rather than sleeping

### Requirement: Console output has a unified minimal API

BigOS SHALL expose a minimal console/TTY output API for ordinary kernel text output while preserving existing early diagnostic direct-output paths.

#### Scenario: Console writes visible text

- **WHEN** kernel code writes a character or string through the console/TTY output API after initialization
- **THEN** BigOS emits the text to the VGA text-mode backend
- **AND** BigOS does not mirror ordinary console text to COM1 serial by default
- **AND** BigOS keeps output behavior freestanding-safe without requiring heap allocation or hosted formatting

#### Scenario: Basic control characters are handled

- **WHEN** console output receives newline, carriage return, tab, or backspace
- **THEN** BigOS applies a documented minimal text-mode behavior for that control character
- **AND** unsupported terminal escape sequences are ignored or emitted literally according to the documented policy

#### Scenario: Early diagnostics remain independent

- **WHEN** early panic, page fault diagnostics, memory self-test markers, or other fatal paths emit deterministic markers
- **THEN** those paths MAY continue using direct VGA/COM1 output APIs
- **AND** they MUST NOT depend on TTY initialization or input buffer state

#### Scenario: Existing direct output remains early-only

- **WHEN** this change introduces the console/TTY output API
- **THEN** existing `kput()` and `kputs()` direct-output APIs retain their early diagnostic semantics
- **AND** ordinary runtime console output uses the new console API rather than changing `kput()` or `kputs()` into console wrappers

### Requirement: TTY and keyboard initialization order is safe

BigOS SHALL initialize keyboard input, TTY buffers, console output, and IRQ unmasking in an order that prevents IRQ1 from reaching an unready input path.

#### Scenario: TTY state exists before keyboard IRQ1 unmask

- **WHEN** BigOS unmasks i8259 IRQ1 for keyboard input or keyboard smoke
- **THEN** the keyboard handler has been registered
- **AND** the TTY input buffer and any required keyboard state have been initialized

#### Scenario: Kernel enables interrupts after selected input readiness

- **WHEN** `kernel()` executes `sti` or equivalent maskable interrupt enable
- **THEN** IDT setup, PIC remap, selected ISR registration, timer readiness, and selected keyboard/TTY readiness have completed

#### Scenario: Default boot remains conservative

- **WHEN** keyboard input is not selected by the default boot policy or validation switch
- **THEN** BigOS keeps keyboard IRQ1 masked
- **AND** the absence of keyboard input MUST NOT prevent timer IRQ0, normal boot marker, memory self-test, or diagnostic exception paths from working

### Requirement: Keyboard and TTY validation is reproducible

BigOS SHALL validate the keyboard/TTY/console change with source-level checks, cross-toolchain builds, and bounded emulator smoke when available.

#### Scenario: Source checks cover IRQ safety

- **WHEN** this change is implemented
- **THEN** tests or static checks confirm keyboard IRQ1 handler registration precedes IRQ1 unmask
- **AND** tests or static checks confirm the keyboard ISR does not directly call `kprintf`, `kput`, allocation APIs, blocking waits, or `mdelay()`

#### Scenario: Source checks cover input behavior

- **WHEN** this change is implemented
- **THEN** tests or static checks cover representative set-1 scancode to ASCII mappings
- **AND** tests or static checks cover modifier state, input buffer FIFO behavior, empty reads, and full-buffer overflow policy

#### Scenario: Build validation covers default and keyboard modes

- **WHEN** keyboard/TTY/console sources are changed
- **THEN** validation includes the narrowest useful `xmake` or cross-toolchain build for default configuration
- **AND** validation includes a build with keyboard input or `keyboard_smoke` enabled when that switch exists

#### Scenario: Manual runtime smoke is recorded

- **WHEN** Bochs, ROM paths, disk image generation, VGA/serial oracle, and manual keyboard input are available
- **THEN** validation records bounded evidence that keyboard input reaches the TTY path and visible console output or marker appears
- **AND** validation MUST NOT require `tools.bigosdev` automatic keyboard scancode injection during this change
- **AND** if runtime smoke cannot run, validation records the missing dependency and remaining keyboard/TTY runtime risk

### Requirement: TTY provides non-interrupt blocking input wait
BigOS SHALL add a blocking TTY input consumer path for ordinary non-interrupt kernel threads while preserving the existing IRQ-safe producer and non-blocking read behavior.

#### Scenario: Empty TTY read may block in thread context
- **WHEN** ordinary non-interrupt kernel code requests a blocking TTY input read and the input buffer is empty
- **THEN** BigOS MUST put the current thread on a TTY input wait queue or equivalent wait object
- **AND** the thread MUST become non-runnable until input arrives, timeout expires, or the wait is cancelled

#### Scenario: Existing non-blocking read remains available
- **WHEN** kernel code uses the existing non-blocking TTY poll or drain API
- **THEN** an empty input buffer MUST still return a non-blocking empty result
- **AND** this behavior MUST NOT depend on scheduler progress or blocking primitives

#### Scenario: Keyboard IRQ wakes TTY waiter safely
- **WHEN** keyboard IRQ1 enqueues translated input into the TTY input buffer and one or more threads wait for input
- **THEN** BigOS MAY wake a waiting TTY consumer through a bounded IRQ-safe wakeup path
- **AND** the keyboard IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, depend on user-mode services, or directly perform a context switch

### Requirement: TTY blocking validation is deterministic
BigOS SHALL validate blocking TTY input behavior without requiring an interactive keyboard path for automated smoke.

#### Scenario: Synthetic producer wakes blocking reader
- **WHEN** TTY blocking smoke is enabled
- **THEN** validation MUST use a deterministic producer, timeout, or documented manual input path to wake a blocked reader
- **AND** the smoke MUST emit fixed `BIGOS_` markers that identify the reader blocked, input arrived or timeout expired, and the reader resumed

#### Scenario: Manual keyboard validation is optional
- **WHEN** emulator keyboard injection or manual input is unavailable
- **THEN** validation MUST record the missing input capability and the source/build checks that still cover IRQ-safe producer boundaries

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
- **AND** 该行为 MUST NOT 依赖动态分配、完整 terminal escape 支持、termios 或 job-control 状态

### Requirement: TTY input exposes terminal events without widening IRQ work

BigOS SHALL extend the TTY input path so the default terminal can represent printable input and a bounded set of control-character events needed by shell/userland consumers. Keyboard IRQ1 MUST remain an IRQ-safe producer: it MAY classify and enqueue fixed-size input records or characters, but it MUST NOT perform ordinary echo, terminal policy, shell cancellation, process signaling, or dynamic terminal state updates that require non-interrupt context.

#### Scenario: Control input is enqueued as bounded data

- **WHEN** keyboard IRQ1 observes a supported control-key combination for the default terminal
- **THEN** BigOS MUST enqueue a bounded character or input event representation into TTY-owned fixed-capacity state
- **AND** the IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, call `kprintf`, perform ordinary console output, or depend on user-mode progress

#### Scenario: Terminal policy remains outside IRQ

- **WHEN** EOF-like, interrupt-like, newline, or backspace-like input is later consumed
- **THEN** the non-interrupt terminal consumer or shell path MUST decide the observable result
- **AND** keyboard IRQ1 MUST NOT directly kill processes, rewrite shell state, or emit ordinary prompt/echo output

### Requirement: Console echo and editing feedback use non-interrupt paths

BigOS SHALL keep printable input echo, line-end feedback, and backspace/delete-like visual feedback in non-interrupt terminal, console, or userland consumer paths. The feedback MUST be bounded and deterministic enough for manual validation, while preserving existing early diagnostic output independence.

#### Scenario: Printable echo is outside IRQ

- **WHEN** a printable character reaches the default terminal input buffer from keyboard IRQ1
- **THEN** BigOS MUST NOT echo that character directly from IRQ context
- **AND** any visible echo MUST come from a non-interrupt terminal consumer, shell line-input path, or another documented non-IRQ console path

#### Scenario: Editing feedback remains minimal

- **WHEN** the user enters newline, carriage return, backspace, delete-like input, or unsupported control bytes on the default terminal
- **THEN** BigOS MUST produce deterministic bounded feedback or documented no-op behavior from a non-interrupt path
- **AND** the behavior MUST NOT require terminal escape support, termios, process groups, sessions, dynamic allocation in IRQ context, or full POSIX terminal state

### Requirement: TTY 输入支持有界非字符控制事件

BigOS SHALL extend the TTY input path so keyboard input can represent a bounded set of non-character terminal control events, including scrollback navigation events. These events MUST coexist with existing printable ASCII input, control-character handling, FIFO input buffering, and blocking/non-blocking read behavior.

#### Scenario: 扩展键被转换为 terminal control event

- **WHEN** keyboard IRQ1 receives a supported PS/2 set-1 extended scancode sequence for PageUp, PageDown, Home, End, or another documented scrollback navigation key
- **THEN** BigOS MUST classify it as a bounded terminal control event rather than corrupting printable input
- **AND** the event MUST be placed in fixed-capacity TTY-owned state or delivered through an equivalent bounded terminal event path

#### Scenario: 字符读取 API 保持兼容

- **WHEN** existing kernel or userland stdin paths read ordinary characters from the TTY input stream
- **THEN** printable characters, line end, backspace/delete-like input, EOF-like input, and interrupt-like input MUST keep their documented behavior
- **AND** scrollback navigation events MUST NOT be returned as spurious printable bytes by character-only read APIs unless a documented compatibility mapping explicitly requires it

#### Scenario: 输入缓冲满时控制事件确定性处理

- **WHEN** a scrollback navigation key arrives and the fixed-capacity TTY event/input buffer is full
- **THEN** BigOS MUST drop the new event or record a deterministic overflow according to the documented TTY overflow policy
- **AND** BigOS MUST NOT overwrite unread printable input unless that policy is explicitly documented

### Requirement: scrollback 控制保持 IRQ-context safe

BigOS SHALL keep keyboard IRQ1 handling for scrollback keys bounded and IRQ-context safe. The IRQ path MAY classify supported scancodes and enqueue fixed-size events, but MUST NOT redraw VGA text output, adjust large console state, allocate memory, block, sleep, or execute shell/userland policy.

#### Scenario: IRQ 只入队翻页事件

- **WHEN** keyboard IRQ1 receives a supported scrollback navigation scancode
- **THEN** the IRQ handler MUST perform only bounded decoder state updates and fixed-capacity event enqueue or equivalent bounded handoff
- **AND** the IRQ handler MUST return to the IRQ dispatch layer for EOI without performing ordinary console output or viewport redraw

#### Scenario: 非中断路径调整 viewport

- **WHEN** a scrollback navigation event is consumed outside IRQ context
- **THEN** BigOS MAY adjust the console scrollback viewport and redraw the visible VGA text window
- **AND** that redraw MUST occur in a context that does not violate keyboard ISR allocation, blocking, or bulk-output constraints

#### Scenario: unsupported extended scancode remains safe

- **WHEN** keyboard IRQ1 receives an unsupported extended scancode sequence
- **THEN** BigOS MUST drop or record the unsupported input deterministically
- **AND** it MUST NOT panic, allocate memory, block, corrupt TTY input state, or change the scrollback viewport

### Requirement: scrollback 输入验证可复现

BigOS SHALL validate scrollback input handling through source-level checks, build checks, and optional emulator interaction when available. Validation MUST cover the classification boundary between printable input and scrollback navigation events without requiring complete automated keyboard injection support.

#### Scenario: 源码级检查覆盖扩展键事件

- **WHEN** this change is implemented
- **THEN** tests or static checks MUST cover representative PS/2 set-1 extended scancode handling for scrollback navigation keys
- **AND** tests or static checks MUST confirm that keyboard IRQ code does not directly call VGA redraw, filesystem, allocation, blocking wait, `mdelay()`, hosted runtime, or shell/userland policy paths

#### Scenario: 构建检查覆盖默认和键盘路径

- **WHEN** keyboard, TTY, console, or VGA text sources are changed
- **THEN** validation MUST include the narrowest useful default build or cross-toolchain syntax check
- **AND** validation SHOULD include a keyboard-enabled or console-scrollback validation configuration when such a switch exists

#### Scenario: emulator 交互验证可记录缺失依赖

- **WHEN** QEMU or Bochs, ROM/display support, disk image generation, and keyboard input are available
- **THEN** validation MUST record bounded evidence that scrollback navigation keys can change the visible console viewport
- **AND** if emulator interaction is unavailable, validation MUST record the missing dependency, the source/build checks that still ran, and the remaining runtime interaction risk

### Requirement: TTY 输入消费受 terminal mode 控制

BigOS SHALL route default terminal input through a mode-aware consumer boundary. Canonical mode SHALL preserve existing bounded shell input semantics; raw mode SHALL minimize kernel interpretation and deliver available bytes or event-derived bytes to userland.

#### Scenario: canonical mode 使用既有控制字符策略

- **WHEN** default terminal fd `0` is read while canonical mode is active
- **THEN** BigOS MUST preserve the existing documented behavior for printable input, line end, backspace/delete-like input, EOF-like input, interrupt-like input, and unsupported controls
- **AND** ordinary echo and feedback MUST remain outside keyboard IRQ context

#### Scenario: raw mode 绕过 canonical 控制策略

- **WHEN** default terminal fd `0` is read while raw mode is active
- **THEN** BigOS MUST deliver available input bytes or event-derived bytes without applying canonical line editing, EOF-like empty-read conversion, or interrupt-like signal targeting
- **AND** raw delivery MUST remain bounded by the fixed TTY input buffer and user buffer length

#### Scenario: mode 切换不清空无关输入

- **WHEN** the terminal input mode changes between canonical and raw
- **THEN** BigOS MUST preserve unread input unless a documented deterministic flush policy is explicitly chosen
- **AND** the mode switch MUST NOT corrupt the TTY ring indices, dropped counter, wait queue, or keyboard decoder state

### Requirement: raw mode 保持 IRQ producer 安全

BigOS SHALL keep keyboard IRQ1 handling bounded and IRQ-context safe regardless of canonical or raw mode. Raw mode MUST NOT move line discipline, echo, signal delivery, shell policy, or bulk terminal state changes into IRQ context.

#### Scenario: IRQ 不读取 mode 后执行复杂策略

- **WHEN** keyboard IRQ1 receives input while raw mode is active
- **THEN** the IRQ handler MAY enqueue bounded characters or fixed-size events
- **AND** it MUST NOT allocate memory, block, sleep, call filesystem services, perform ordinary console output, signal processes, or directly execute shell policy

#### Scenario: raw wakeup 使用既有 bounded wait path

- **WHEN** raw mode input is enqueued and a user process is blocked reading fd `0`
- **THEN** BigOS MAY wake the reader through the existing bounded IRQ-safe wakeup path
- **AND** the wakeup MUST NOT perform a direct context switch or blocking operation from keyboard IRQ1

### Requirement: raw mode 与 scrollback 控制分离

BigOS SHALL make terminal mode determine whether supported navigation keys are consumed by console scrollback policy or delivered to userland. Canonical mode MAY retain current scrollback behavior; raw mode MUST reserve userland ownership for supported navigation keys selected by the terminal input policy.

#### Scenario: canonical scrollback 行为保持

- **WHEN** canonical mode is active and a supported scrollback navigation event is consumed by the non-interrupt terminal consumer
- **THEN** BigOS MAY adjust the console viewport and redraw the visible console
- **AND** userland character reads MUST NOT observe partial bytes for the consumed viewport operation

#### Scenario: raw navigation input 交给用户态

- **WHEN** raw mode is active and keyboard input produces a supported navigation event selected for userland delivery
- **THEN** BigOS MUST expose a documented byte or fixed sequence through fd `0`
- **AND** it MUST NOT adjust the console viewport for that event

