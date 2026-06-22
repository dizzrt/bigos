## ADDED Requirements

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
