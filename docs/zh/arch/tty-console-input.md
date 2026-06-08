# TTY、Console 与键盘输入

BigOS 阶段 2 的输入路径只覆盖单核 x86_64、i8259 PIC、PS/2 set-1 keyboard 和 VGA text mode。目标是提供最小、可验证的 keyboard 到 TTY handoff，以及普通运行期文本输出入口；它不是完整终端、shell 或用户态输入子系统。

## 输入数据流

```text
keyboard IRQ1
  -> irq_dispatch()
  -> isr_keyboard()
  -> inb(0x60)
  -> input::handle_keyboard_scancode()
  -> PS/2 set-1 bounded decode
  -> terminal::enqueue_input()
  -> non-interrupt consumer read_char()/drain()
  -> optional blocking consumer read_char_blocking()
```

keyboard ISR 只读取一个 scancode byte，更新固定 decoder 状态，并在产生受支持字符时入队到 TTY 输入缓冲。成功入队后，TTY 层可以通过 bounded scheduler wakeup helper 唤醒一个 blocked reader。ISR 不调用 `kprintf()`、`kput()`、VGA/serial 输出、动态分配、阻塞等待、`mdelay()`、filesystem、syscall、用户态相关路径或直接 context switch。

## Scancode 策略

当前 decoder 支持最小 US-layout PS/2 set-1：

- 可打印字母、数字、常用符号和空格。
- Enter、Backspace、Tab、Escape。
- Shift、Ctrl、Alt 的 make/break 状态更新；modifier scancode 本身不产生字符。
- Ctrl 仅对代表性字母和少量控制键输出 C0 控制字符。

扩展 scancode 前缀 `0xe0`/`0xe1` 和未映射 scancode 被记录到 unsupported counter 并丢弃。decoder 不 panic、不分配、不阻塞，也不会把未知 scancode 写入 TTY buffer。

## TTY 输入缓冲

TTY 输入缓冲是静态固定容量 ring buffer，容量为 `TTY_INPUT_CAPACITY`。IRQ producer 使用 `terminal::enqueue_input()` 写入，非中断 consumer 使用 `terminal::read_char()` 或 `terminal::drain()` 读取。

Overflow 策略是确定性的：当 ring buffer 满时丢弃新输入并递增 drop counter，不覆盖 unread input。空 buffer 读取返回 `false` 或 `0`，不 sleep、不等待 scheduler，也不依赖进程或用户态。

## Blocking Consumer

阶段 10 以 additive 方式增加 `terminal::read_char_blocking()` 非中断 API。它会先尝试既有 non-blocking `read_char()` 路径；如果输入缓冲为空，则通过 `sched::wait_queue_wait_until()` 挂入 TTY input wait queue，并在 IRQ disabled 状态下检查 predicate，避免 empty check 和入队之间漏掉 producer wakeup。

blocking API 只能在 `sched::can_block()` 允许的普通 running kernel-thread 上下文调用。成功写出字符时返回 `1`，否则返回 timeout、invalid argument 或 forbidden blocking context 等确定性负 wait error。既有 `read_char()` 与 `drain()` 仍保持非阻塞，不依赖 scheduler 进度。

自动化 blocking smoke 使用 synthetic producer 调用 `terminal::enqueue_input()`，因此不依赖手工键盘输入也能覆盖同一 TTY wakeup 路径。手工键盘验证仍是可选项，使用时需要记录 emulator input capability。

## Console 输出边界

普通运行期文本输出使用 `terminal::console_put()` 和 `terminal::console_write()`，当前 backend 只写 VGA text mode，不默认 mirror 到 COM1 serial。这样 serial 仍保留给 bounded marker、smoke 和 fatal diagnostic。

基础控制字符行为：

- `\n`：移动到下一行行首。
- `\r`：移动到当前行行首。
- `\t`：向后移动 4 个字符位置。
- `\b`：在非起始位置回退一格并擦除该字符。
- Unsupported escape sequence：不解析 ANSI/VT 序列；Escape 作为普通字符写入或由上层决定忽略。

`kput()`、`kputs()`、`kprintf()`、`serial_puts()` 和 fatal/page-fault/memory self-test marker 路径保留 early direct output 语义，不依赖 TTY 初始化或 input buffer 状态。

## 初始化顺序

`kernel()` 当前顺序为：

```text
VGA clear
serial_init()
init_mem()
optional BIGOS_MM_SELF_TEST
optional BIGOS_USER_VMEM_SMOKE
terminal::init_tty()
irq::initIRQ()
optional BIGOS_PAGE_FAULT_SMOKE trigger
irq::enableIRQ()
normal boot marker
optional syscall / scheduler / user-program smoke
sched::start()  (idle thread owns halt; replaces the bare hlt loop)
```

`serial_init()` 在普通 boot path 中显式完成 early COM1 bring-up，使默认 serial marker 不再依赖 `mm_self_test()` 等间接初始化路径。`terminal::init_tty()` 初始化 input ring、console ready flag 和 keyboard decoder state。`irq::initIRQ()` 随后初始化 IDT/PIC 并注册 timer/keyboard handler。PIC 初始化默认 mask 所有 IRQ line；timer IRQ0 仍在 timer init 中 unmask，keyboard IRQ1 只有在 `BIGOS_KEYBOARD_SMOKE` 显式开启时才 unmask，因此默认 boot 不依赖键盘输入。

## 非目标

阶段 10 只新增上述最小 kernel-thread blocking consumer；仍不实现 line discipline、POSIX terminal read、fd/VFS 集成、用户可见 blocking IO、取消语义或完整 process lifecycle ownership，也不实现多 TTY、完整 ANSI/VT 终端、行编辑、历史记录、shell、USB HID、APIC/IOAPIC、SMP 或国际化 keyboard layout。
