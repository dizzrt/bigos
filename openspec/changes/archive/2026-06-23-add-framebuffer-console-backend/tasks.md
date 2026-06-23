## 1. Backend 边界与现有 VGA 适配

- [x] 1.1 梳理 `kernel/core/terminal/console.cc`、`include/bigos/console.h`、`include/bigos/device.h` 和现有 VGA text 调用点，确认 runtime console state 与 early diagnostic-only 输出边界。
- [x] 1.2 定义内核内部 console render backend 接口，覆盖清屏、单 cell 绘制、viewport 整屏重绘和 cursor cell 更新，保持 public console API 不扩大。
- [x] 1.3 将现有 VGA text backend 适配到新的 render backend 边界，确保 `console_write()`、scrollback、PageUp/PageDown/Home/End 和 clear 行为保持不变。
- [x] 1.4 补充或更新源码级检查，防止 runtime console 绕过 backend 边界直接绑定单一 VGA text 渲染路径。

## 2. Framebuffer Backend 初始化

- [x] 2.1 在内核启动/terminal 初始化顺序中接入 framebuffer console backend probe，消费已校验的 early framebuffer view、glyph lookup view 和显式 device/MMIO mapping API。
- [x] 2.2 校验 framebuffer pixel format、stride、byte-size、write range、cell metrics 和 80x25 text grid bounds，任一条件不满足时确定性回退到 VGA text backend。
- [x] 2.3 确认 framebuffer 物理范围继续保持 reserved/device 语义，renderer 不通过普通 RAM allocator 或未经审计的 direct-map alias 写入。
- [x] 2.4 记录 framebuffer backend 不改变 BootInfo v2 ABI、kernel link address、page-table layout、IDT/syscall vector、磁盘布局或用户态 syscall ABI。

## 3. Glyph 渲染与软件光标

- [x] 3.1 实现 framebuffer cell 清除和 glyph bitmap 绘制，按当前 `char` cell 输入查询 glyph lookup 并写入支持的 framebuffer 像素格式。
- [x] 3.2 为缺失 glyph、空白 cell、不可打印 byte 和 unsupported lookup 状态实现确定性 replacement 或 blank 渲染策略。
- [x] 3.3 实现 framebuffer 软件光标，确保 cursor 不写入 scrollback state，并在 cursor 移动或 viewport 重绘时不会留下残影。
- [x] 3.4 审查 renderer 的 bounds checking、整数溢出、stride 计算、alignment、MMIO 写入顺序和 failure behavior。

## 4. Console/Scrollback 集成

- [x] 4.1 将 `render_viewport()` 和 cursor 更新路径改为使用选中的 render backend，保持 console state、scrollback ring、viewport policy 和 clear policy 单一归属。
- [x] 4.2 验证普通 stdout/stderr、shell prompt、runtime kernel console 输出在 VGA fallback 和 framebuffer backend 下都经过同一 default terminal sink。
- [x] 4.3 验证 PageUp/PageDown/Home/End 对 framebuffer backend 触发整屏重绘，且历史视口收到新输出时 follow policy 与现有行为一致。
- [x] 4.4 确认 early diagnostic-only `kput()`/`kputs()`、panic、fault diagnostics 和 COM1 marker 不依赖 framebuffer console 初始化。

## 5. 文档与 OpenSpec 对齐

- [x] 5.1 更新相关架构文档，说明 default runtime console state 与 VGA/framebuffer render backend 的边界；如修改 `docs/en`，同步对应 `docs/zh`。
- [x] 5.2 在文档和注释中明确本能力不包含 UTF-8 decoding、CJK 显示、codepoint cell、双宽 cell、ANSI/VT、termios、多终端或完整 POSIX terminal。
- [x] 5.3 更新验证记录模板或 notes，区分 VGA fallback、UEFI framebuffer backend、缺失工具 skipped/blocked 和历史诊断。

## 6. 验证

- [x] 6.1 运行 OpenSpec 状态/校验，确认 proposal、design、specs 和 tasks 可被工具识别。
- [x] 6.2 运行针对 terminal/console/framebuffer/glyph 边界的源码级 pytest；所有 Python 相关命令通过 `uv run ...` 执行，若 `uv` 不可用则记录 blocker。
- [x] 6.3 运行默认 `xmake` 构建，确认 Legacy VGA fallback 仍可构建。
- [x] 6.4 对新增或修改的 C++ 源/头文件运行尽量贴近 freestanding x86_64 C++17 配置的 clang/clangd 辅助检查，区分历史诊断、当前变更诊断和工具链配置缺口。
- [x] 6.5 在 QEMU + OVMF 可用时运行 UEFI framebuffer 图形路径验证，记录 framebuffer geometry、backend selection、可见文本、软件光标和 scrollback viewport 行为；不可用时记录缺失工具和剩余风险。
- [x] 6.6 运行或记录 Legacy BIOS/QEMU fallback smoke，确认无 framebuffer 或无字体时仍可到达现有 bounded userland baseline。
