## Context

当前 BigOS 已具备 kernel-owned IDT、i8259 PIC、timer IRQ0、diagnostic exception path、VGA text 输出和 COM1 serial 输出。keyboard IRQ1 仍是 smoke-only：默认 masked，仅在 `keyboard_smoke` 打开时读取 PS/2 data port `0x60` 并输出 scancode marker。

`tmp/terminal-keyboard-wip/` 保存了早期 terminal/console/keyboard 原型，但它不是活动源码：命名在 `terminal`/`pty` 间漂移，keyboard ISR 原型直接调用 `kput()`，并倾向默认启用 IRQ1。这些行为不符合当前 IRQ runtime hardening 后的边界，需要重新设计为最小、可验证、freestanding-safe 的输入/输出路径。

## Goals / Non-Goals

**Goals:**

- 建立 PS/2 keyboard set-1 scancode 到最小 key/ASCII 的输入路径，覆盖普通可打印字符、Enter、Backspace、Tab、Escape 和 Shift/Ctrl/Alt 状态。
- 引入固定容量 TTY input ring buffer，使 keyboard ISR 只做有限 work：读 scancode、更新简单修饰状态、转换可支持事件并入队。
- 引入最小 console/TTY 抽象，统一字符输出、字符串输出和基础控制字符处理，并让 `kernel()` 能初始化该路径。
- 保持 IRQ dispatch 的 EOI 规则不变：keyboard handler 不直接 EOI，external IRQ dispatch 在 handler 返回后统一 EOI。
- 用源码级检查、交叉构建和可用时 Bochs keyboard smoke 记录约束输入路径。

**Non-Goals:**

- 不实现 scheduler、阻塞 sleep、进程、用户态、syscall、shell 或用户程序输入。
- 不实现多 TTY、完整 ANSI/VT 终端、作业控制、行编辑历史、滚动回滚缓冲或虚拟控制台切换。
- 不实现完整键盘 layout 抽象、国际化输入、热插拔、USB HID、APIC/IOAPIC 或 SMP/per-CPU 输入队列。
- 不改变 boot 地址、linker higher-half base、BootInfo ABI、page-table self-mapping、direct-map 或 allocator API 语义。

## Decisions

1. 使用固定容量静态 ring buffer，而不是 heap 分配队列。

   当前 allocator 不承诺 IRQ-context 安全，阶段 2 也不引入 scheduler 或阻塞语义。TTY 输入缓冲应使用静态存储或初始化期明确构造的固定容量对象，ISR 入队在满时丢弃新输入并记录 drop counter 或可诊断状态，避免在中断上下文分配或等待。

   备选方案是 `kmalloc` 动态队列，但这会提前扩大 allocator 的 IRQ 安全契约，属于阶段 3 的范围。

2. keyboard ISR 允许做最小 scancode 解码，但不直接输出。

   ISR 读取 port `0x60` 是硬件必须动作；维护 Shift/Ctrl/Alt press/release 状态和 set-1 基础转换可以避免把 raw scancode 语义泄漏到 TTY 层。ISR 不调用 `kprintf`、不格式化字符串、不写 VGA/serial、不阻塞、不发送 EOI。

   备选方案是 ISR 只入队 raw scancode，由非中断上下文解码。该方案更保守，但会让后续消费路径必须理解硬件 scancode；阶段 2 先采用小而固定的 set-1 解码表，且通过源码级检查限制 ISR 行为。

3. TTY/console 命名统一到一个主线命名空间。

   新增 API 应避免继承 WIP 中 `terminal`/`pty` 混用问题。建议公开 `include/bigos/tty.h` 与 `include/bigos/console.h`，实现放在 `src/kernel/terminal/` 或现有风格一致的路径下，命名空间统一为 `bigos::terminal`。如果实现时选择 `bigos::console`/`bigos::input` 等拆分命名，也必须在文档中固定边界。

4. 普通 console 输出默认只写 VGA，不默认同步到 COM1。

   COM1 serial 继续作为 deterministic marker、smoke 和 early diagnostics 的后端，不作为普通 console 字符流的默认 mirror。这样可以保持 serial oracle 简洁，避免用户输入 echo 或普通日志污染 marker 判定；需要 serial marker 的验证路径应显式调用 bounded marker/diagnostic API。

5. keyboard IRQ1 unmask 必须晚于输入路径就绪。

   `initIRQ()` 可以继续注册 keyboard handler，但 IRQ1 默认保持 masked。只有当 TTY input buffer 初始化完成，且 keyboard smoke 或默认输入路径明确需要 keyboard 时，才 unmask IRQ1。`kernel()` 中 `sti` 前必须完成 IDT/PIC/handler/buffer 的顺序约束。

6. 阶段 2 只记录人工 Bochs keyboard smoke，不扩展 `tools/boot_debug.py` 自动注入 scancode。

   当前阶段的目标是建立内核侧输入/TTY/console 边界；自动键盘注入属于 emulator tooling 能力，容易与既有 serial/VGA oracle 不稳定问题耦合。阶段 2 的 runtime 验证可以记录人工 Bochs 键盘操作步骤和观测结果；若人工输入也不可用，记录缺失依赖和剩余 runtime 风险。

7. `kput()`/`kputs()` 在阶段 2 保留为 early direct output，不迁移为 console wrapper。

   新 console API 作为普通 runtime 输出入口新增；现有 `kput()`/`kputs()`、`serial_puts()` 和 fatal marker 路径继续保留 direct-output 语义。这样可以避免 page fault、panic、memory self-test 等早期诊断路径在 TTY 初始化前后出现语义变化。是否最终把 `kput()`/`kputs()` 包装到 console 层，留给后续 cleanup change。

## Risks / Trade-offs

- [Risk] Bochs keyboard input 和 serial/VGA oracle 在本地环境不稳定，无法稳定端到端验证输入。
  → Mitigation：将源码级检查和构建作为必过项；runtime smoke 不可用时记录缺失依赖和剩余风险，不声明已通过。

- [Risk] 在 ISR 内做 scancode 到 ASCII 转换可能扩大中断路径复杂度。
  → Mitigation：仅支持固定 set-1 基础表和少量修饰键状态；禁止 formatter、输出、分配和阻塞；用测试检查 handler body。

- [Risk] ring buffer 满时丢输入会影响交互体验。
  → Mitigation：阶段 2 以可用和安全为优先，满时丢弃并计数；后续 scheduler/阻塞读阶段再引入 backpressure 或 wait queue。

- [Risk] console 层封装可能影响现有 early diagnostics。
  → Mitigation：保持现有 `serial_puts`、`kput`/`kputs`、fatal marker 路径 direct-output 语义；console 只作为普通运行时输出入口新增。

## Migration Plan

1. 固化 WIP 取舍：只迁移 scancode/keymap 中可验证的最小 set-1 表和控制字符常量，丢弃 `pty` 命名和 ISR 直写输出模式。
2. 新增 keyboard/input 与 TTY/console headers 和实现，使用静态 ring buffer，提供初始化、ISR 入队、非中断上下文消费和 console 写 API。
3. 修改 keyboard ISR，将 smoke-only 直写输出改为受控入队；`keyboard_smoke` 下通过非中断上下文消费或 bounded marker 观测。
4. 修改 `kernel()` 初始化顺序，启用 TTY/console 初始化，并确保 IRQ1 unmask 晚于 handler 和 buffer readiness。
5. 更新源码级测试、文档和构建开关验证；Bochs 可用时记录人工 keyboard smoke 操作步骤和结果，不在本阶段扩展 `tools/boot_debug.py` 自动注入 scancode。

Rollback 策略：如果 TTY/console 接入导致 bootability 下降，保留 `keyboard_smoke` 默认关闭和 IRQ1 masked，可以回退到仅注册 handler 但不 unmask 的状态；early VGA/serial diagnostics 不依赖新 console 层。
