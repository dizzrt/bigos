## Context

BigOS 当前默认终端路径是单一 console/TTY：keyboard IRQ1 只做有界输入生产，`tty.cc` 负责输入 ring 和控制事件消费，`syscall.cc` 将 fd `0` 的默认 stdin 接到 `read_char_blocking()`，fd `1/2` 接到 runtime console output。已有 process/session/foreground group 能力能把 interrupt-like input 定向到前台进程组，但还没有“前台程序是否接管输入解释”的模式边界。

现在的行为接近固定 canonical mode：输入由 TTY/shell 消费路径解释，退格和换行产生基础反馈，Ctrl-C 可以面向 foreground group，scrollback 控制键被内核 console 消费。后续若要支持 ANSI 导航键序列和简单 TUI，必须先能让前台程序明确进入 raw mode，否则 PageUp/PageDown、方向键、Ctrl-C、Backspace 的归属会长期硬编码在内核里。

目标数据流：

```text
keyboard IRQ1
        |
        v
bounded key decode
        |
        v
TTY input ring / terminal events
        |
        v
terminal mode gate
   | canonical: line/control policy + echo + scrollback
   | raw: byte/event delivery to userland, minimal kernel interpretation
        |
        v
read(fd=0) -> user buffer
```

本 change 不修改 boot 地址、linker 地址、IDT/syscall vector、页表布局、磁盘布局、CR3 切换或 early diagnostic-only 输出路径。新增 syscall 必须以 append-only 方式扩展现有 syscall number 枚举和 libc wrapper。

## Goals / Non-Goals

**Goals:**

- 为默认控制台终端增加固定大小 mode state，至少包含 canonical/raw 两种输入模式。
- 提供最小用户态控制面：查询当前终端模式、设置终端模式，并通过 libc wrapper 暴露给简单静态 C 程序。
- canonical mode 保持现有默认 shell 可用性：基础回显、line end、backspace/delete-like、EOF-like、interrupt-like 和 scrollback 控制。
- raw mode 让前台程序接管输入：不等待 Enter、不做普通回显、不自动将 Ctrl-C 转信号、不将可交付导航键固定消费为 scrollback。
- 定义 `fork`、`execve`、进程退出、foreground group 恢复时的模式继承/恢复策略，避免 shell 被永久留在 raw mode。
- 为 `add-vt-ansi-console` 留出清晰集成点：raw mode 下可以交付方向键/PageUp/PageDown 等 ANSI 序列；canonical mode 下保留内核默认 scrollback。

**Non-Goals:**

- 不实现完整 POSIX `termios`、`tcgetattr`/`tcsetattr` 全量结构、`VMIN/VTIME`、baud rate、parity、serial line discipline、流控或 modem 控制。
- 不引入多 TTY、伪终端、`/dev/tty`、后台读写控制、完整 job control 或完整 shell 行编辑。
- 不实现 ANSI/VT 输出 parser；该能力由 `add-vt-ansi-console` 承载。
- 不改变 syscall 既有号位、寄存器 ABI、syscall vector、EOI 语义、boot handoff、页表布局或 early diagnostic 行为。

## Decisions

### Decision: 使用 BigOS-specific terminal mode，而不是 POSIX termios 结构

新增内核状态只表达 BigOS 当前需要的输入解释策略，例如：

- `TerminalInputMode::Canonical`
- `TerminalInputMode::Raw`

用户态 ABI 暴露一个小结构或位掩码，例如 mode flags 和 size/version 字段；内核只接受已知 version/size/flag，未知位返回确定性错误。

理由：完整 `struct termios` 会带来大量暂不支持的字段和兼容承诺。BigOS 现在只有单一默认控制台终端，最小 mode ABI 更符合 bounded OS 的成熟度。

替代方案：直接定义 POSIX 形状的 `struct termios`，只实现少数字段。该方案会让用户误以为 `ICANON`、`ECHO`、`ISIG`、`VMIN/VTIME` 等都有标准语义，后续兼容债更大。

### Decision: terminal mode 归属默认终端，而不是每个进程私有

模式状态放在默认 terminal state 中，由允许的前台进程或 shell 设置。`fork` 和 `execve` 不复制出独立 terminal mode；新镜像观察同一个默认终端当前 mode。

理由：当前 BigOS 没有多 TTY/pty，fd `0` 默认路径就是单一控制台。把 mode 绑定到 terminal 而不是 process，可以匹配“前台程序接管当前终端”的模型，也避免同一 terminal 被多个进程看到不同解释状态。

替代方案：每进程保存 raw/canonical。该方案对单一默认终端反而会制造不一致：同一个键盘输入到底按哪个进程的模式解释，需要完整 foreground/background 读写控制才能说清。

### Decision: 只允许当前 session 的 foreground group 改 terminal mode

`set_terminal_mode` 应检查调用进程存在、默认终端 foreground group 有效，并要求调用进程属于当前 foreground group。受限 shell/session leader 恢复路径只允许把 mode 恢复为 canonical，不能借此切换到 raw。失败返回确定性 errno，不改变旧 mode。

理由：raw mode 是用户可见 terminal state，后台进程不能随意把 shell 的输入模式改掉。当前已有 foreground group 能力，复用它可以保持边界明确。

替代方案：任何进程都能设置。该方案实现简单，但会让后台或已退出子进程污染交互终端，尤其容易让 shell 卡在 raw mode。

### Decision: raw mode 最小语义是不解释、不回显、逐字节交付

raw mode 下，fd `0` 默认 read 应尽快返回 1 到 `len` 个当前已缓冲字节或固定序列字节，而不是等待 line end，也不为填满用户缓冲继续等待。内核不做普通 echo，不把 Ctrl-C 自动转 foreground group signal，不把 EOF-like 转 empty read，不把用户态应接管的导航键固定消费为 scrollback。

canonical mode 下，现有行为继续保持：基础行输入、回显、Ctrl-C/EOF-like、scrollback 控制由非 IRQ 的 terminal/shell 路径处理。

替代方案：只增加“关闭 echo”或“关闭 Ctrl-C”的局部 flag。该方案会很快逼近 termios 字段组合，且每个组合都需要定义完整交互语义。先做 canonical/raw 二态更稳。

### Decision: 提供异常/退出后的确定性恢复策略

为了避免前台程序进入 raw mode 后崩溃导致 shell 不可用，内核和 shell 协作需要有恢复边界：

- 默认 boot 和 shell 启动时 terminal mode 为 canonical。
- foreground command 可以设置 raw mode。
- foreground group 完全退出、被 reap 或 shell 恢复 foreground group 时，默认 terminal mode 必须回到 canonical，或 shell 必须通过 `bigos_tcsetmode` 显式恢复 canonical 并有失败处理。
- `execve` 不隐式恢复 mode，因为 TUI 程序经常在 exec 后继续需要 raw mode。

理由：这比“每个 syscall 自动恢复”更符合终端模型，同时避免失败路径卡死。

替代方案：进程退出不恢复，完全交给用户程序。该方案对早期 BigOS 交互体验风险过高。

## Risks / Trade-offs

- [Risk] raw mode 程序崩溃后 shell 不可用 → Mitigation: foreground group 退出/reap 或 shell 恢复 foreground 时强制/显式恢复 canonical，并加入 smoke。
- [Risk] 后台进程修改 terminal mode → Mitigation: `set_terminal_mode` 检查 current process、session 和 foreground group，失败不改变旧状态。
- [Risk] raw mode 与现有 Ctrl-C 信号路径冲突 → Mitigation: canonical mode 保留 signal targeting；raw mode 只交付字节/事件，不自动 signal。
- [Risk] PageUp/PageDown 与 scrollback 归属不清 → Mitigation: spec 明确 canonical mode 可消费 scrollback，raw mode 交给用户态或后续 ANSI 序列层。
- [Risk] 用户误以为支持完整 termios → Mitigation: headers、docs、OpenSpec 均使用 BigOS-specific bounded terminal mode 命名，不声明完整 POSIX `termios`。
- [Risk] 新 syscall ABI 影响稳定性 → Mitigation: append-only 增加 syscall number，source-level 检查号位和 wrapper，保持 vector/寄存器/EOI 语义不变。

## Migration Plan

1. 在 `include/bigos/tty.h` 和 `kernel/core/terminal/tty.cc` 增加固定 terminal mode state 与 getter/setter。
2. 扩展 syscall number、dispatch、用户指针校验和 libc wrapper，暴露最小 mode get/set。
3. 调整 fd `0` 默认 read 路径：canonical mode 保持现有行为；raw mode 逐字节返回并跳过 echo/signal/EOF-like 策略。
4. 调整 foreground group 生命周期和 shell 运行 foreground command 的恢复路径，确保回到 canonical。
5. 增加 source-level tests、用户态 smoke 和文档同步。

回滚策略：若 raw mode 导致默认 shell 回退，可以保留 syscall 号但让 `set_terminal_mode(raw)` 返回 `-ENOSYS` 或 `-EINVAL`，恢复旧 canonical-only 行为；不得改变已存在 syscall 号位或默认 fd I/O 路径。

### Decision: libc wrapper 使用 BigOS-specific 命名

用户态 libc wrapper 固定命名为 `bigos_tcgetmode` 和 `bigos_tcsetmode`，暴露 BigOS-specific mode 结构或常量，不提供 POSIX 风格 `tcgetattr` / `tcsetattr` 名称。

理由：当前能力只是 canonical/raw 二态和少量 BigOS 语义，不包含 POSIX `struct termios` 的 `ICANON`、`ECHO`、`ISIG`、`VMIN/VTIME`、baud rate、line discipline 等语义。使用 BigOS-specific 命名能避免错误兼容承诺，并给未来完整 termios 留出独立演进空间。

替代方案：直接提供 `tcgetattr` / `tcsetattr`。该方案对移植程序表面友好，但会让调用方自然期待完整 POSIX 行为，不适合当前 bounded terminal mode。

## Open Questions

- 无。
