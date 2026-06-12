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
  -> fd 0 未安装文件时的默认用户态 stdin
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

Stage 20 将同一个 blocking consumer 接到默认用户态 stdin：当用户进程读取 fd `0` 且该描述符没有安装文件或 pipe 时，`SYS_READ` 会在 TTY ring 上阻塞，并向用户态返回一个有界 byte。如果 fd `0` 通过 `dup2()` 或重定向替换为 pipe/file，读取会走普通 fd/VFS 路径，而不是默认 console。

## Console 输出边界

普通运行期文本输出使用 `terminal::console_put()` 和 `terminal::console_write()`，当前 backend 写 VGA text mode。Stage 20 在 fd `1` 或 fd `2` 没有安装 file/pipe 时，将用户态写入路由到这个可见 console；已重定向的描述符仍走普通 fd/VFS 路径。syscall 路径也保留现有 bounded serial write marker，使 headless smoke 仍能观察默认 userland 进度。console API 本身不默认 mirror 到 COM1 serial，serial 仍保留给 bounded marker、smoke 和 fatal diagnostic。

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

`serial_init()` 在普通 boot path 中显式完成 early COM1 bring-up，使默认 serial marker 不再依赖 `mm_self_test()` 等间接初始化路径。`terminal::init_tty()` 初始化 input ring、console ready flag 和 keyboard decoder state。`irq::initIRQ()` 随后初始化 IDT/PIC 并注册 timer/keyboard handler。PIC 初始化默认 mask 所有 IRQ line；timer IRQ0 仍在 timer init 中 unmask，keyboard IRQ1 在 handler 注册后为默认交互 shell 路径 unmask。自动化 headless 验证仍不要求手工键盘输入。

## 最小交互 Console

默认交互 shell 路径刻意保持在 POSIX terminal 范围以下：

- Keyboard IRQ1 只解码并入队受支持 byte，然后执行 bounded TTY wakeup。
- Printable input、newline feedback 和 backspace feedback 由 `read(0, ...)` 返回后的非中断 shell consumer 产生。
- `/bin/sh` 只在 fd `0` 与 fd `1` 仍绑定到默认 console fast path 时显示确定性的 `$ ` prompt；pipe 或重定向文件会抑制 prompt。
- 当 fd `1` 与 fd `2` 未被重定向时，stdout 和 stderr 会通过 VGA text mode 可见。

## 非目标

该路径不实现多 TTY、完整 ANSI/VT terminal、命令历史、termios、job control、terminal process group、USB HID、APIC/IOAPIC、SMP 或国际化 keyboard layout。最小 fd 集成只覆盖有界用户态的默认 console fast path，不引入 `/dev/tty`、通用 character-device filesystem、terminal signal、取消语义、async I/O 或完整 POSIX terminal read。
