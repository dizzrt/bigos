# TTY、Console 与键盘输入

BigOS TTY console input capability 的输入路径只覆盖单核 x86_64、i8259 PIC、PS/2 set-1 keyboard 和默认 runtime text console。目标是提供最小、可验证的 keyboard 到 TTY handoff，以及普通运行期文本输出入口；它不是完整终端、shell 或用户态输入子系统。

## 输入数据流

```text
keyboard IRQ1
  -> irq_dispatch()
  -> isr_keyboard()
  -> inb(0x60)
  -> input::handle_keyboard_scancode()
  -> PS/2 set-1 bounded decode
  -> terminal::enqueue_input() / terminal::enqueue_input_record()
  -> fixed TerminalInputRecord ring
  -> default terminal mode gate
  -> canonical read_char()/read_char_blocking() 或 raw read_raw_available_blocking()
  -> fd 0 未安装文件时的默认用户态 stdin
```

keyboard ISR 只读取一个 scancode byte，更新固定 decoder 状态，并在产生受支持字符或有界 terminal-control event 时以固定大小 terminal input record 入队到 TTY 输入缓冲。成功入队后，TTY 层可以通过 bounded scheduler wakeup helper 唤醒一个 blocked reader。ISR 不调用 `kprintf()`、`kput()`、VGA/serial 输出、动态分配、阻塞等待、`mdelay()`、filesystem、syscall、用户态相关路径、直接 context switch 或 console viewport redraw。

## Scancode 策略

当前 decoder 支持最小 US-layout PS/2 set-1：

- 可打印字母、数字、常用符号和空格。
- Enter、Backspace、Tab、Escape。
- Shift、Ctrl、Alt 的 make/break 状态更新；modifier scancode 本身不产生字符。
- Ctrl 仅对代表性字母和少量控制键输出 C0 控制字符。
- `0xe0` 扩展方向键、Home、End、Delete、PageUp 和 PageDown 会表示为有界 terminal-control event，并在前台程序读取时展开为文档化的 ANSI escape byte sequence。
- `Shift+PageUp` 和 `Shift+PageDown` 是保留的内核 scrollback 快捷键，会作为私有 kernel scrollback event 消费，不向 stdin 泄漏 PageUp/PageDown escape bytes。

不支持的扩展 scancode 序列和未映射 scancode 会记录到 unsupported counter 并丢弃。decoder 不 panic、不分配、不阻塞、不重绘 VGA 输出，也不会把未知 scancode 写入 TTY buffer。

## TTY 输入缓冲

TTY 输入缓冲是静态固定容量 ring buffer，容量为 `TTY_INPUT_CAPACITY`。IRQ producer 使用 `terminal::enqueue_input()` 或 `terminal::enqueue_input_record()` 写入；非中断 consumer 可以通过 `terminal::read_input_record()` 读取原始有界 record，也可以继续使用 byte-compatible 的 `terminal::read_char()` 和 `terminal::drain()` helper。

每个 `TerminalInputRecord` 要么是 character record，要么是 control record。有界 control 子集包括 line end、backspace、delete-like、EOF-like、interrupt-like、navigation-key event、私有 kernel scrollback PageUp/PageDown event 和 unsupported control。byte-compatible consumer 会把 `\r` 归一为 line end，EOF-like input 在默认 console stdin 上表现为确定性的 empty-read result，interrupt-like input 以有界 `0x03` byte 交给 shell 取消当前行，navigation control 会展开为固定 escape sequence，`Shift+PageUp`/`Shift+PageDown` scrollback control 由非中断 character consumer 消费并调整 console viewport，unsupported control 由 terminal consumer 作为确定性 no-op 消费。

Navigation sequence 策略：

- 方向键：`ESC [ A`、`ESC [ B`、`ESC [ C`、`ESC [ D`。
- Home 和 End：`ESC [ H`、`ESC [ F`。
- Delete：`ESC [ 3 ~`。
- PageUp 和 PageDown：`ESC [ 5 ~`、`ESC [ 6 ~`。
- Sequence expansion 使用 TTY consumer 内的固定 pending state；如果 ring 无法接收固定 control record，则丢弃整个 key event，不向用户态暴露 partial escape sequence。

Overflow 策略是确定性的：当 ring buffer 满时丢弃新输入并递增 drop counter，不覆盖 unread input。空 buffer 读取返回 `false` 或 `0`，不 sleep、不等待 scheduler，也不依赖进程或用户态。

## Terminal 输入模式

单一默认 console terminal 拥有固定大小的 BigOS terminal mode state。
`terminal::init_tty()` 会把它初始化为 canonical mode；该状态不依赖动态分配、
filesystem 访问、用户态进度或当前选择的显示 backend。

Canonical mode 是默认 shell 模式。它保留既有 line-oriented 行为：普通可打印输入
按字节返回给 shell，newline 与 backspace/delete-like feedback 由非中断 consumer
处理，EOF-like input 可以变成 empty read，interrupt-like input 可以向当前
foreground group 投递有界 `SIGINT`，受支持 scrollback control 可以被消费为
console viewport 操作。

Raw mode 是 BigOS-specific 的前台程序输入归属边界。当 fd `0` 仍使用默认 console
fast path 时，raw read 在至少有一个 byte 可用后返回一个或多个当前可用 byte；它不
等待填满整个用户缓冲，也不等待 Enter。Raw mode 不做普通 echo，不把 EOF-like input
转换成 empty read，不为 Ctrl-C 自动向 foreground group 发 signal，也不把受支持
默认 navigation key 消费为 scrollback。方向键、Home/End、Delete、PageUp 和
PageDown 会以有界 ANSI escape byte sequence 暴露给用户态；只有
`Shift+PageUp` 与 `Shift+PageDown` 保留为内核 scrollback 快捷键。

模式控制使用 `SYS_TCGETMODE` 和 `SYS_TCSETMODE`，libc 暴露为
`bigos_tcgetmode()` 与 `bigos_tcsetmode()`，并使用 `struct bigos_terminal_mode`。
该对象包含 size、version、mode 与 flags 字段；未知 size、version、flag 或 mode
会确定性失败，且不改变旧模式。设置 mode 要求调用者属于当前默认终端 foreground
process group。session leader recovery path 只能恢复 canonical mode，使 `/bin/sh`
在前台命令退出或失败后可以恢复。`fork` 与 `execve` 不创建私有 terminal mode
object；子进程和替换后的镜像观察同一个默认终端状态。

该接口不是 POSIX `termios`：它不暴露 baud rate、`VMIN/VTIME`、serial line
discipline、pseudo-terminal、terminal database、多终端设备、后台读写控制或完整
job-control 行为。

## Blocking Consumer

blocking primitives and timer ownership capability 以 additive 方式增加 `terminal::read_char_blocking()` 非中断 API。它会先尝试既有 non-blocking `read_char()` 路径；如果输入缓冲为空，则通过 `sched::wait_queue_wait_until()` 挂入 TTY input wait queue，并在 IRQ disabled 状态下检查 predicate，避免 empty check 和入队之间漏掉 producer wakeup。

blocking API 只能在 `sched::can_block()` 允许的普通 running kernel-thread 上下文调用。成功写出字符时返回 `1`，遇到有界 EOF-like terminal event 时返回 `0`，否则返回 timeout、invalid argument 或 forbidden blocking context 等确定性负 wait error。既有 `read_char()` 与 `drain()` 仍保持非阻塞，不依赖 scheduler 进度。

自动化 blocking smoke 使用 synthetic producer 调用 `terminal::enqueue_input()`，因此不依赖手工键盘输入也能覆盖同一 TTY wakeup 路径。手工键盘验证仍是可选项，使用时需要记录 emulator input capability。

交互控制台可用性将同一组 blocking consumer 接到默认用户态 stdin：当用户进程读取 fd `0` 且该描述符没有安装文件或 pipe 时，`SYS_READ` 会在 TTY ring 上阻塞，并根据 terminal mode 返回 canonical byte 或 raw 当前可用 byte。如果 fd `0` 通过 `dup2()` 或重定向替换为 pipe/file，读取会走普通 fd/VFS 路径，而不是默认 console。

## Console 输出边界

普通运行期文本输出使用 default terminal sink `terminal::default_terminal_write()`，它包装 `terminal::console_put()` 和 `terminal::console_write()`。runtime console 统一拥有固定容量 cell storage、display attributes、有界 ANSI/CSI parser、UTF-8 decoder state、cursor position、saved cursor coordinates、256 行内核 scrollback buffer、viewport policy 和 clear policy；可见列数和行数由选中的内部 render backend 提供。VGA text 仍是固定 80x25 Legacy fallback；UEFI framebuffer text backend 只有在 framebuffer metadata 已校验、通过显式 `map_device_mmio()` 完成映射、像素格式是受支持的 32-bit RGBX/BGRX、glyph lookup view 可用，并且能从 framebuffer 几何计算出有界完整 cell grid 时才会被选择。交互控制台可用性在 fd `1` 或 fd `2` 没有安装 file/pipe 时，将用户态写入路由到同一默认 console 路径；已重定向的描述符仍走普通 fd/VFS 路径。syscall 路径也保留现有 bounded serial write marker，使 headless smoke 仍能观察默认 userland 进度。console API 本身不默认 mirror 到 COM1 serial，serial 仍保留给 bounded marker、smoke 和 fatal diagnostic。

基础控制字符行为：

- `\n`：移动到下一行行首。
- `\r`：移动到当前行行首。
- `\t`：用确定性的空白 cell 推进到下一个 4 列 tab stop。
- `\b`：擦除前一个逻辑字符，必要时同时清理双宽字符的两个 cell。
- 支持的 ANSI/CSI 输出子集：SGR reset/default、前景色 `30-37`、背景色 `40-47`、bright 前景色 `90-97`、bright 背景色 `100-107`、光标移动 `CUU/CUD/CUF/CUB`、one-based `CUP/HVP`、erase display `ED`、erase line `EL`、`CSI s/u` 和 `ESC 7/8`。
- Unsupported 或 malformed escape sequence：有界 parser 会 reset 到普通文本状态或丢弃不支持的 control sequence；后续普通 UTF-8 输出不需要 terminal reset 即可恢复。

当输出越过最后一个可见行时，runtime console 更新自己拥有的 scrollback state，并要求 backend 重绘完整可见 viewport。VGA backend 使用硬件 text cursor；framebuffer backend 在动态可见 grid 上从 console-owned Unicode codepoint cell 渲染 glyph pixels，并以 backend state 绘制软件光标，不把 cursor byte 存入 scrollback。保留的 `Shift+PageUp` 与 `Shift+PageDown` 快捷键使用由当前可见行数派生的有界步长。查看历史时产生的新输出不会破坏保留历史，也不会强制把 viewport 拉回底部。受支持的 console clear path 会通过选中的 backend 清屏、丢弃保留的 runtime scrollback，并把 viewport 重置到底部；framebuffer backend 会覆盖整块已校验 mapped framebuffer，避免 text grid 外继续显示固件残留像素。

runtime console 会在 escape parser 分类后把普通输出按有界 UTF-8 解码，存储 Unicode codepoint cell，并记录单宽、双宽 leading、双宽 trailing、空白或 replacement cell role。新写入 cell 会复制当前 SGR 派生的 display attributes；已有 cell 在被重写或擦除前保持自己的属性。字形宽度优先使用 kernel glyph lookup 的 width class：半宽 glyph 占一个 cell，全宽 glyph 占两个 cell。缺失或无效 codepoint 优先使用 `U+FFFD` replacement glyph；若该 glyph 不可用，再确定性降级为 `?` 或 blank。framebuffer backend 通过 glyph lookup 和确定性 RGB color 渲染这些 console-owned cell 与属性；Legacy VGA text backend 直接显示 printable ASCII，把同一前景/背景属性映射为确定性 VGA color byte，并对非 ASCII 或 trailing cell 做确定性降级。render backend 不拥有 ANSI parser、UTF-8 decoder、scrollback、TTY input 或 terminal mode state。

`kput()`、`kputs()`、`kprintf()`、`serial_puts()` 和 fatal/page-fault/memory self-test marker 路径保留 early direct output 语义，不依赖 TTY 初始化、framebuffer console 初始化、glyph lookup 可用性或 input buffer 状态。

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

`serial_init()` 在普通 boot path 中显式完成 early COM1 bring-up，使默认 serial marker 不再依赖 `mm_self_test()` 等间接初始化路径。`terminal::init_tty()` 初始化 input ring、console ready flag 和 keyboard decoder state。`irq::initIRQ()` 随后初始化 IDT/PIC 并注册 timer/keyboard handler。PIC 初始化默认 mask 所有 IRQ line。APIC default-delivery 配置下，keyboard IRQ1 通过 IOAPIC 路由到已初始化且 online 的 BSP，并以 LAPIC EOI 完成；文档化 BSP-only fallback 下，keyboard IRQ1 在 handler 注册后走 PIC path unmask。自动化 headless 验证仍不要求手工键盘输入。

## 最小交互 Console

默认交互 shell 路径刻意保持在 POSIX terminal 范围以下：

- Keyboard IRQ1 只解码并入队固定大小 input record，然后执行 bounded TTY wakeup。
- 默认 navigation key 是固定大小 TTY control record，会展开为给用户态的 ANSI escape byte；只有 `Shift+PageUp` 和 `Shift+PageDown` 是私有 kernel scrollback control。viewport 移动和整屏重绘只在非中断 terminal consumer 处理这些私有 control 时发生。
- Printable input、newline feedback、backspace feedback、EOF-like exit、interrupt-like line cancellation 和 unsupported-control no-op 行为由 `read(0, ...)` 返回后的非中断 terminal 或 shell consumer 产生。
- 默认终端保存一个数值型 foreground `pgid`。查询和变更只在普通 syscall/用户进程上下文执行；IRQ1 不遍历 process group、不执行 shell 策略、不分配、不阻塞，也不做文件系统 I/O。
- Interrupt-like input 在 canonical mode 下仍向 consumer 返回有界 `0x03` byte，并尝试向当前 foreground group 进行有界 `SIGINT` 投递；raw mode 只把该 byte 交给用户态，不自动 signal。foreground group 缺失或为空时只产生确定性 no-op/错误结果，不解引用悬垂进程对象。
- `/bin/sh` 在前台命令或 pipeline 结束并恢复 shell foreground group 后，会把单一默认终端恢复为 canonical mode。
- `/bin/sh` 只在 fd `0` 与 fd `1` 仍绑定到默认 console fast path 时显示确定性的 `$ ` prompt；pipe 或重定向文件会抑制 prompt。
- 当 fd `1` 与 fd `2` 未被重定向时，stdout 和 stderr 会通过选中的默认 console render backend 可见。

## 非目标

该路径不实现多 TTY、完整 xterm/VT100/VT220 行为、命令历史、完整 POSIX `termios`、伪终端、完整 job control、后台读写控制、USB HID、完整图形 terminal 行为、locale、Unicode normalization、grapheme cluster、shaping、输入法、持久化或无限历史、APIC/IOAPIC、SMP 或国际化 keyboard layout。最小 fd 集成只覆盖有界用户态的默认 console fast path，不引入 `/dev/tty`、通用 character-device filesystem、async I/O 或完整 POSIX terminal read。
