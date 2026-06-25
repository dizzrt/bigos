## 1. Console 状态模型

- [x] 1.1 梳理 `kernel/core/terminal/console.cc` 当前 cursor、scrollback、viewport、UTF-8 decoder 与 render backend 调用关系，记录实现前不变量和现有 source tests 覆盖点。
- [x] 1.2 将 console cell 显示属性抽象为固定大小结构，替代固定 `CONSOLE_COLOR` 写入路径，并保持普通 ASCII/UTF-8 输出行为不变。
- [x] 1.3 引入 VT-ready console state：当前属性、保存光标、可寻址 visible grid 坐标、parser state，并保持固定容量、不依赖动态分配。
- [x] 1.4 调整自动换行、换行、回车、tab、backspace、clear、scrollback redraw，使其在带属性 cell 和可寻址 cursor 下保持现有语义。

## 2. 输出侧 ANSI/CSI 子集

- [x] 2.1 在 console 输出入口实现固定状态机，支持 Ground、Escape、CSI 参数收集和确定性 reset/recovery。
- [x] 2.2 实现 SGR 子集：reset、默认属性、前景色 `30-37`、背景色 `40-47`、bright 前景色 `90-97`、bright 背景色 `100-107`，并记录 VGA fallback 的确定性降级策略。
- [x] 2.3 实现 cursor movement/positioning 子集：`CUU`、`CUD`、`CUF`、`CUB`、`CUP`、`HVP`，并对越界目标执行 clamp 或文档化拒绝。
- [x] 2.4 实现 erase/save/restore 子集：`ED`、`EL`、`CSI s/u` 和 `ESC 7/8`；save/restore 只保存和恢复 cursor row/col，不保存 SGR 属性或其他 terminal mode。
- [x] 2.5 补充 parser 边界测试：跨 write 序列、参数过多、数字过长、未知 final byte、非法序列后普通文本恢复、UTF-8 与 escape 状态切换。

## 3. Render Backend 属性渲染

- [x] 3.1 更新 `include/bigos/console_render.h` 和相关 backend 接口，使 VGA/framebuffer 后端接收带显示属性的 console-owned cell。
- [x] 3.2 更新 VGA text backend 的属性映射，保持默认白字黑底兼容并支持 SGR 颜色的确定性 VGA color byte。
- [x] 3.3 更新 framebuffer backend 的前景/背景颜色映射，覆盖普通与 bright 前景/背景色，确保所有像素写入仍受 framebuffer 映射范围、stride、grid bounds 约束。
- [x] 3.4 验证 framebuffer backend 仍只作为 renderer，不保存 ANSI parser、UTF-8 decoder、scrollback 或 TTY 输入状态。

## 4. TTY 输入序列

- [x] 4.1 梳理 `keyboard.cc` 扩展 scancode、Shift 修饰状态、现有 `TerminalControl` 消费路径和 BigOS canonical/raw mode，确认方向键、Home/End、Delete、PageUp/PageDown 默认交给前台用户程序，`Shift+PageUp`/`Shift+PageDown` 保留为内核 scrollback。
- [x] 4.2 为方向键、Home/End、Delete、PageUp/PageDown 实现固定 ANSI byte sequence 或固定事件到 byte sequence 的转换，保持 IRQ handler allocation-free、non-blocking、no ordinary console output；为 `Shift+PageUp`/`Shift+PageDown` 实现不泄漏 stdin bytes 的固定 scrollback event。
- [x] 4.3 为 fixed escape sequence 入队实现容量检查策略，确保缓冲不足时不会向用户态暴露误导性的 partial sequence。
- [x] 4.4 保持 Ctrl-C、EOF-like、newline、backspace、BigOS raw/canonical mode 与默认 shell 输入反馈行为不回退；默认导航键不得再被隐式消费为内核 scrollback 控制，`Shift+PageUp`/`Shift+PageDown` 除外。

## 5. 文档与规范同步

- [x] 5.1 更新 `docs/en/arch/tty-console-input.md`，把“不支持 ANSI/VT”改为 bounded ANSI/VT subset 支持矩阵、输入序列策略和非目标。
- [x] 5.2 同步更新 `docs/zh/arch/tty-console-input.md`，保持与英文文档同路径语义一致。
- [x] 5.3 如涉及 Unicode cell 或 framebuffer 文档描述，更新对应 `docs/en` 与 `docs/zh` 镜像，明确 backend renderer-only 边界和属性渲染限制。
- [x] 5.4 检查文档、注释和 OpenSpec 文本不得声明完整 xterm、VT100/VT220、termios、pseudo-terminal、多 TTY 或完整 POSIX terminal。

## 6. Validation

- [x] 6.1 运行相关 source-level tests，至少覆盖 terminal/TTY/console/parser 规则；Python 测试命令使用 `uv run pytest ...`，若 `uv` 不可用则记录 blocker 和剩余风险。
- [x] 6.2 运行默认 `xmake` 构建，确认 `x86_64-elf-gcc/x86_64-elf-g++` 工具链可用；若不可用则记录缺失工具和未验证风险。
- [x] 6.3 对修改的 C++ 源和头文件执行 clang 辅助检查，尽量使用 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include path；区分历史诊断、当前变更诊断和 freestanding false positives。
- [x] 6.4 对修改的 C++ 源和头文件执行 clangd 辅助诊断或记录 clangd 配置不可用原因；clang/clangd 不替代 xmake cross build。
- [x] 6.5 运行至少一个 QEMU headless smoke，优先通过 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/... --expect-serial-marker BIGOS_USER_EXEC` 或等价项目命令记录默认用户态仍可启动。
- [x] 6.6 在可用环境中执行 QEMU 或 Bochs 图形 console 手动/半自动验证，观察 SGR 颜色、清屏、光标定位、方向键输入序列；若显示、ROM、Bochs/QEMU 或磁盘镜像依赖不可用，记录跳过原因和剩余 console-usability 风险。
- [x] 6.7 汇总 validation notes，分开列出通过项、无法运行项及原因、历史诊断、当前变更新增问题和残余风险。
