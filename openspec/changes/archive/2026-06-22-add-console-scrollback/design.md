## Context

BigOS 当前默认运行路径已经具备 VGA 文本输出、COM1 serial marker、键盘 PS/2 set-1 解码、TTY 输入缓冲、默认 console output API、用户态 `write` 到 stdout/stderr 的 runtime console sink，以及 `/bin/sh` 交互路径。现有输出链路是 `console_write()` 逐字符调用 video text backend，VGA driver 直接读写 `0xb8000` 文本显存并维护硬件光标；当输出越过 80x25 可见区域时，没有自动上卷、没有 scrollback 历史、也没有键盘翻页事件。

该设计只面向当前 x86_64 Legacy BIOS VGA text mode baseline。它不改变 boot handoff、link address、page-table layout、direct map、IDT/IRQ/syscall vector、CR3 切换、磁盘布局、VGA 文本显存基址、COM1 serial marker 路径或用户态 syscall ABI。UEFI backend parity、图形模式 console、多终端和完整 terminal 语义不在本 change 范围内。

关键数据流：

```text
user/kernel text output
        |
        v
terminal::console_write()
        |
        v
bounded console screen/scrollback state
        |
        v
device::write_video_text() / vga text render
        |
        v
0xb8000 visible 80x25 text buffer

keyboard IRQ1
        |
        v
bounded scancode classification
        |
        v
TTY input record or terminal control event
        |
        v
non-interrupt terminal consumer adjusts viewport
```

## Goals / Non-Goals

**Goals:**

- 让 runtime console 在输出越过最后一行时自动上卷，保证新输出仍可见且光标保持在 80x25 可见范围内。
- 为默认 console 维护固定容量 scrollback 历史，使普通 shell 输出、用户程序 stdout/stderr 和 runtime kernel console 输出可被有界回看。
- 支持 PageUp/PageDown/Home/End 键盘控制事件调整 scrollback 视口，并在视口变化后重绘当前可见窗口。
- 保持 keyboard IRQ1 只做有界解码和入队，不在 IRQ 中执行 VGA 重绘、shell 策略、动态分配或阻塞操作。
- 保持 early diagnostics、panic、COM1 serial marker 和现有 fd/syscall 语义稳定。
- 增加源码级、构建级和可选 emulator 交互验证。

**Non-Goals:**

- 不实现完整 ANSI/VT100 escape parser、颜色属性状态机、终端 alternate screen、光标相对移动 escape、scroll region 或完整 line discipline。
- 不实现 `termios`、多终端、伪终端、后台读写控制、完整 job control、完整 POSIX terminal API 或新的用户态 terminal syscall ABI。
- 不支持动态扩张或持久化的无限历史，不把 scrollback 写入文件系统。
- 不改变 VGA text mode 的硬件常量、boot/linker 地址、interrupt vectors、syscall ABI、disk layout 或 UEFI backend 边界。
- 不要求自动化工具注入真实 PageUp/PageDown 键盘事件；交互行为可在 emulator 可用时通过手工记录或后续脚本化路径验证。

## Decisions

1. **把自动上卷放在 console/VGA text 输出边界，而不是交给 shell 或用户程序分页。**

   - 理由：输出越界是 console backend 的基础行为，不应要求每个用户程序使用 `more` 或自行限制输出。kernel runtime 输出和 shell prompt 同样需要可见性保证。
   - 替代方案：先实现用户态 pager。该方案只能处理管道化命令输出，不能解决内核诊断、shell prompt 或普通 stdout/stderr 已经写入 console 后的屏幕丢失。

2. **使用固定容量内核内存 scrollback，而不是按需分配历史页。**

   - 理由：terminal 输出可能发生在早期或低层路径，固定容量缓冲更符合 freestanding kernel 的可预期边界。容量耗尽时丢弃最旧历史即可，行为确定。
   - 替代方案：用 `kmalloc`/page allocator 动态扩容。该方案历史更长，但会把分配失败、不可阻塞上下文和内存压力引入 console path，不适合当前默认终端能力。

3. **用“逻辑行/单元 ring + viewport”管理 scrollback，而不是只依赖 VGA 硬件 start address。**

   - 理由：VGA 硬件滚屏只能覆盖有限文本显存窗口，且不自然表达超过显存页数的历史。内核维护逻辑历史后，可以统一处理自动上卷、清屏、重绘和 PageUp/PageDown/Home/End。
   - 替代方案：直接使用 CRT controller start address 做硬件滚屏。该方案快速但历史容量受 VGA 显存布局限制，和现有 `put_cell` 直接写可见 offset 的模型耦合更强。

4. **键盘扩展键进入 TTY/terminal event，再由非中断 consumer 调整视口。**

   - 理由：PageUp/PageDown/Home/End 是 `E0` 扩展 scancode，当前键盘 decoder 会将其记为 unsupported。将其分类为固定事件可复用现有 TTY input record 模型，同时保持 IRQ producer 有界。
   - 替代方案：在 IRQ handler 中直接调用 console scrollback 函数。该方案会让 IRQ 路径执行重绘和潜在大批量 VGA 写入，违反现有 keyboard ISR 边界。

5. **用户处于历史视口时，新输出遵循确定性 follow policy。**

   - 理由：scrollback 需要定义“正在看历史时又有输出”的行为。默认策略采用“用户在底部时自动跟随最新输出；用户翻到历史时保持历史视口，并用后续 PageDown/End 或等价事件回到底部”。这样不会在阅读历史时突然跳动。
   - 替代方案：任何新输出都强制回到底部。该方案简单，但会破坏 scrollback 的主要交互价值。

6. **early diagnostic-only 输出路径可以保持直接 VGA/COM1，不强制纳入 scrollback。**

   - 理由：panic、早期 fault 和 smoke marker 的优先级是确定性可见和 serial oracle，不应依赖 terminal 初始化或 scrollback 状态。runtime console 输出纳入 scrollback 即可覆盖交互体验。
   - 替代方案：所有 `kput()`/`kputs()` 都改为 console wrapper。该方案会模糊 early diagnostics 与 runtime terminal 的边界，也可能影响早期初始化顺序。

## Risks / Trade-offs

- [Risk] console path 引入固定历史缓冲后，字符输出成本增加。
  Mitigation: 使用简单静态 ring 和 80x25 有界重绘；普通追加输出只更新必要 cell，viewport 变化才整屏重绘。

- [Risk] 自动上卷和 scrollback 同时存在时容易出现 off-by-one，导致光标越界或历史行错乱。
  Mitigation: 明确光标位置、逻辑行数、可见高度和历史容量的不变量；用源码级测试覆盖换行、长行、最后一列、backspace、clear 和容量回绕。

- [Risk] PageUp/PageDown/Home/End 扩展键处理可能破坏现有 ASCII 输入 FIFO 语义。
  Mitigation: 将非字符键表示为独立 `TerminalInputKind` 或专用控制事件；字符读取 API 对非字符事件采取忽略或专用消费策略，保持现有 `read_char` 行为。

- [Risk] 在用户翻看历史时新输出不可见，可能让用户误以为系统卡住。
  Mitigation: 记录或显示有界“有新输出”状态可以作为后续增强；本 change 至少要求 PageDown/End 或等价控制可回到底部，且底部视口默认自动跟随。

- [Risk] 运行时验证依赖交互键盘，CI 环境可能不可用。
  Mitigation: 以源码级 pytest、交叉构建和可合成的 console scrollback 单元/源码检查作为主验证；QEMU/Bochs 交互 smoke 可用时记录，缺失时明确 skipped/blocked。

## Migration Plan

1. 定义 console screen/scrollback 状态和不变量，保持固定容量、静态存储和初始化顺序。
2. 先实现自动上卷和光标边界保护，确保普通输出超过 80x25 后仍可见。
3. 接入 scrollback ring 与 viewport 重绘，使 runtime console 输出进入历史缓冲。
4. 扩展键盘 decoder 和 TTY input record，支持 PageUp/PageDown/Home/End 或等价有界 terminal control event。
5. 在非中断 terminal consumer 中处理视口调整，并保持字符读取 API 兼容。
6. 补充源码级测试、构建验证和可选 emulator 交互记录。

回滚策略：保留 console API 和 TTY 输入接口不变，禁用 scrollback state 更新和 viewport 控制即可回退到仅自动上卷；如自动上卷也需回退，可恢复 VGA driver 的旧直接写入路径，但应保留边界保护测试用于暴露历史问题。

## Resolved Decisions

- 固定 scrollback 容量默认采用 256 行，兼顾内存占用和交互价值；后续如需调整容量，应保持固定容量和确定性 overflow 语义。
- 本 change 同时支持 PageUp/PageDown 和 Home/End：PageUp/PageDown 按页调整历史视口，Home/End 跳转到 retained history 顶部和最新输出底部。
- `kput()`/`kputs()` 保持 early diagnostic-only 独立路径，不进入 scrollback；implementation 需要审查调用点并记录 runtime console 与 early diagnostic 的边界。
