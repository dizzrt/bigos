## Context

BigOS 当前默认运行时文本控制台已经不再只是早期 VGA 字符输出：`kernel/core/terminal/console.cc` 拥有固定容量 scrollback、viewport、UTF-8 解码、Unicode codepoint cell、双宽 cell 角色和清屏/滚动行为；`kernel/core/terminal/tty.cc` 与 `kernel/core/terminal/keyboard.cc` 负责 IRQ-safe keyboard producer、TTY input record 和默认 shell stdin；`kernel/core/syscall/syscall.cc` 将默认 fd `0/1/2` 接到 console/TTY fast path。

现有边界仍然明确排除 ANSI/VT。若要支持彩色输出、清屏、光标定位和方向键序列，不能只在 `console_put()` 中识别几个字符串；需要把 console 状态整理成 VT-ready 的数据模型，并把输出解析、cell 属性、输入序列和 render backend 边界分开。

数据流目标：

```text
user write(fd=1/2)
        |
        v
SYS_WRITE default console fast path
        |
        v
terminal::default_terminal_write()
        |
        v
VT parser -> UTF-8 decoder -> screen/scrollback cells
        |
        v
console render backend (VGA or framebuffer)
```

输入流目标：

```text
keyboard IRQ1
        |
        v
bounded key decode
        |
        v
TTY input record / fixed escape bytes
        |
        v
read(fd=0) -> user buffer
```

本 change 不修改 boot 地址、linker 地址、IDT/syscall vector、页表布局、disk layout、CR3 切换规则或 early diagnostic-only 输出路径。

## Goals / Non-Goals

**Goals:**

- 建立 VT-ready console state：当前属性、光标位置、保存光标、自动换行策略、可寻址 visible screen 与固定容量历史保持一致。
- 在输出侧实现常用有界 ANSI/CSI 子集：SGR、CUU/CUD/CUF/CUB、CUP/HVP、ED、EL、SCP/RCP。
- 将 `ConsoleRenderCell` 的颜色/属性从固定常量改为当前 SGR 状态驱动，VGA 和 framebuffer 后端都从 cell 属性渲染。
- 在输入侧为方向键、Home/End、Delete、PageUp/PageDown 等导航键提供用户态可见的 ANSI 转义序列，并保持 keyboard IRQ1 handler 不分配、不阻塞、不输出普通 console 文本。
- 为无效、过长、未知 escape 序列定义确定性恢复，不允许 parser 卡死或污染后续 UTF-8 输出。
- 更新中英文文档与 source-level 测试，明确支持矩阵和非目标。

**Non-Goals:**

- 不声明完整 xterm、VT100、VT220 或 POSIX terminal 兼容。
- 不实现 OSC/DCS/APC/PM、alternate screen、复杂 DEC private modes、鼠标协议、bracketed paste、完整功能键矩阵。
- 不新增 POSIX `termios`、伪终端、多 TTY、`/dev/tty`、shell 命令历史或完整行编辑；已有 BigOS-specific canonical/raw terminal mode 只作为输入交付策略边界使用，不扩展为完整 `termios`。
- 不让 framebuffer backend 拥有 VT 状态；后端只渲染 console-owned cells。
- 不改变 syscall ABI、interrupt vector、EOI 规则、boot handoff、内存布局或 early diagnostic 语义。

## Decisions

### Decision: 使用固定状态机解析输出侧 ANSI/CSI

`console_put()` 之后的字节入口应先进入 console-owned VT parser。parser 至少包含 Ground、Escape、CSI 参数收集三个状态，参数数组和中间字节容量固定。Ground 状态下按现有 UTF-8 decoder 处理普通文本和基本控制字符；Escape/CSI 状态下只收集 bounded ASCII 控制序列。

理由：逐字节 parser 能自然处理 `SYS_WRITE_MAX_LEN` 边界和跨 write 的序列拆分，不需要临时堆缓冲，也不要求用户态配合。

替代方案：在 syscall 层按字符串扫描 ANSI 序列。该方案会把终端协议耦合到 user buffer copy 边界，难以处理跨 write 序列，也会让 kernel-internal console writes 与用户态 writes 行为不一致。

失败行为：参数过多、数字过长、未知 final byte 或非法中间字节必须重置到 Ground。未知序列默认丢弃该控制序列；普通后续字节必须继续可显示或可解析。

### Decision: 保留固定容量 scrollback，但引入可寻址 visible screen 更新

当前模型接近顺序文本流；VT 光标移动和擦除需要能修改当前可见区域内任意 cell。实现应把 console state 分为：

- 固定容量 retained lines，用于 scrollback 和历史保留。
- 当前可见 viewport 对应的 screen/cell 访问函数。
- 光标位置、保存光标、当前属性、底部跟随策略。

光标定位、擦除屏幕、擦除行只作用于 bounded visible grid 或当前保留行范围，不引入无界历史或文件持久化。自动换行仍由 console state 统一处理。

替代方案：把每次 VT 操作直接映射成 backend draw，不更新 console-owned cells。该方案会破坏 scrollback/viewport 重绘一致性，framebuffer cursor 也会出现 stale pixels。

失败行为：任何光标目标超出 visible grid 时必须 clamp 到有效范围；擦除操作不能写出 `CONSOLE_RENDER_MAX_WIDTH` 或 retained line 数组边界。

### Decision: 将显示属性作为 cell 数据传给后端

SGR 解析只更新 console 当前属性；后续写入 cell 时复制该属性。默认运行时路径已经优先选择 framebuffer backend，framebuffer backend 将 `30-37`、`40-47`、`90-97`、`100-107` 映射为确定性的前景/背景像素颜色。VGA backend 仍作为 fallback，把同一属性集合降级为确定性的 VGA text color byte；如果硬件 blink/intensity 限制导致 bright background 不能逐色等价，也必须采用文档化映射而不是拒绝解析。

替代方案：让后端保存当前 SGR 状态。该方案会让 VGA 和 framebuffer 行为分叉，并破坏 backend 作为 renderer-only 的边界。

失败行为：不支持的 SGR 参数必须忽略或重置到默认属性；颜色映射必须保持确定性，不能访问动态调色板或 hosted runtime。

### Decision: 输入侧导航键交给前台用户程序

BigOS 已经具备 BigOS-specific canonical/raw terminal mode：`SYS_TCGETMODE`/`SYS_TCSETMODE` 暴露单一默认终端的 canonical/raw 输入模式，`/bin/sh` 在前台命令结束后恢复 canonical。因此本 change 采用常见终端惯例：方向键、Home/End、Delete、PageUp/PageDown 等默认导航键都转为固定 ANSI escape byte 序列，并通过现有 TTY/default stdin 路径交给当前前台用户程序；`Shift+PageUp` 与 `Shift+PageDown` 保留为内核 console scrollback 快捷键，不进入用户态 stdin。raw mode 下前台程序可直接消费默认导航键序列；canonical mode 下也不得把默认导航键默认为 console viewport 控制。

替代方案：在 shell 内部合成方向键行为。该方案只能服务 `/bin/sh`，无法让其他用户程序获得终端输入协议。

失败行为：TTY ring 满时按既有溢出策略丢弃整条固定序列，并记录 dropped；IRQ handler 仍然不得分配、阻塞、调用 console output 或依赖用户态进度。`Shift+PageUp`/`Shift+PageDown` 的 scrollback handoff 仍需保持 IRQ-safe：IRQ 路径只入队固定控制事件，实际 viewport 调整和 redraw 发生在非中断消费路径。

### Decision: 两组保存/恢复光标序列都只保存 row/col

`ESC 7`/`ESC 8` 与 `CSI s`/`CSI u` 都应作为 save/restore cursor 兼容形式支持，但保存内容仅包含 cursor row 和 column，不保存当前 SGR 属性、字符集、scroll region 或其他终端模式。属性状态只由 SGR 序列改变，restore cursor 不产生隐式颜色回滚。

替代方案：保存 row/col 之外连同 SGR 属性一起保存。该方案更接近部分终端的扩展行为，但会让“保存光标”影响颜色状态，增加实现和测试复杂度。

失败行为：restore 目标必须 clamp 到当前 visible grid；没有有效保存状态时 restore 使用默认 `(0, 0)` 或文档化 no-op 策略，不能访问未初始化状态。

### Decision: 验证以 source-level 与有界 runtime smoke 组合为主

parser、SGR、cursor、erase、输入序列、IRQ-safe 边界适合 source-level 测试或静态检查。可视颜色和光标行为需要 QEMU/Bochs 图形证据或受控用户态输出程序；headless serial marker 只能证明路径执行，不能证明像素/颜色正确。

替代方案：只靠人工图形观察。该方案不适合作为回归基础，也难以覆盖 parser 边界条件。

## Risks / Trade-offs

- [Risk] VT parser 与 UTF-8 decoder 状态交错导致非法序列后输出错位 → Mitigation: 明确 Ground 状态才进入 UTF-8 decoder，Escape/CSI 失败统一 reset，并用跨 write 测试覆盖。
- [Risk] 光标定位破坏现有 scrollback bottom-follow 语义 → Mitigation: 让可寻址操作只更新 console-owned cells，并统一走 viewport redraw；对历史 viewport 收到新输出的策略写入测试。
- [Risk] 输入侧固定序列被 TTY ring 部分写入导致用户态看到残缺 escape → Mitigation: 为固定序列提供原子 enqueue helper，容量不足时整条丢弃或记录明确策略。
- [Risk] VGA fallback 与 framebuffer bright background 表现不完全一致 → Mitigation: framebuffer 作为优先后端支持 `100-107`，VGA fallback 采用文档化确定性降级；规范不要求跨后端色彩校准。
- [Risk] 范围扩大为完整 terminal → Mitigation: specs 明确 bounded subset 和非目标，文档不得宣称完整 xterm/POSIX terminal。
- [Risk] 图形 runtime 验证依赖本机 QEMU/Bochs/OVMF/显示能力 → Mitigation: tasks 要求记录无法运行的检查、原因和残余风险，并保留 source/build 检查作为最低证据。

## Migration Plan

1. 先重构 console cell 属性和状态结构，保持现有普通文本、UTF-8、scrollback、clear 行为不变，并准备将默认导航键交给用户态。
2. 加入 VT parser，但先让不支持序列按确定性策略处理，再逐项启用 SGR、cursor、erase 操作。
3. 扩展 render backend 消费 cell 属性，先保证 VGA fallback，再验证 framebuffer 映射。
4. 扩展 keyboard/TTY 输入序列，并保持已有 default shell 输入、Ctrl-C、EOF-like、backspace 行为。
5. 更新 `docs/en` 与 `docs/zh`，同步 OpenSpec spec，补 source-level tests 与可用 runtime smoke。

回滚策略：若 VT parser 或输入序列导致默认 shell 可用性回退，可以在实现层保留一个 compile-time 或局部开关以恢复旧的普通文本路径；回滚不得改变已经稳定的 syscall/IRQ/boot ABI。

## Open Questions

- 无。当前决策采用行业惯例：默认导航键进入前台用户程序，`Shift+PageUp`/`Shift+PageDown` 保留为内核 console scrollback 快捷键。
