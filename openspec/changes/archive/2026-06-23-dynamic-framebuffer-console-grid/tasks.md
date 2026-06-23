## 1. Backend Grid Boundary

- [x] 1.1 梳理 `include/bigos/console_render.h`、`kernel/core/terminal/console.cc`、`kernel/core/terminal/console_render.cc` 和相关 tests，确认当前固定 80x25、scrollback、cursor、viewport 与 framebuffer bounds 的耦合点。
- [x] 1.2 扩展 internal render backend 边界，使 backend 能报告 visible columns/rows；VGA backend 固定报告 80x25。
- [x] 1.3 为 runtime console 定义编译期最大 columns/rows 和最小可用 viewport，避免 framebuffer 分辨率导致动态分配或无界静态存储增长。
- [x] 1.4 将 console state 的 visible width/height 初始化为 selected backend grid，并确保 public console/terminal API 不扩大。

## 2. Dynamic Console State

- [x] 2.1 将 console cell storage、line clear、render viewport、cursor set、line wrap 和 double-width cell placement 改为使用当前 visible columns/rows，同时保留固定容量上限。
- [x] 2.2 更新 bottom viewport、clamp viewport、PageUp/PageDown/Home/End 和 history-view new-output policy，使 page step 与当前 visible rows 一致。
- [x] 2.3 更新 console clear，使其重置动态 viewport/cursor 并通过 selected backend 执行 full clear 或对应 backend clear。
- [x] 2.4 审查固定容量、line slot、cursor index、双宽行尾、tab stop、scrollback rollover 和整数边界，确认不会越界或动态增长。

## 3. Framebuffer Backend Full Clear And Dynamic Grid

- [x] 3.1 在 framebuffer backend probe 中基于 framebuffer width/height 与 glyph cell metrics 计算 dynamic columns/rows，并按最大 grid 上限 clamp。
- [x] 3.2 校验 dynamic grid 的 pixel bounds、stride、byte-size、mapping.length 和整数乘法溢出；任何不一致必须回退 VGA。
- [x] 3.3 实现 full framebuffer clear，覆盖整块已验证 mapped framebuffer，避免固件图形残留。
- [x] 3.4 更新 framebuffer cell range、fill、glyph draw、software cursor 和 viewport redraw，使其使用 dynamic grid 和 full clear 后的背景。
- [x] 3.5 保持 Legacy VGA fallback、COM1 serial diagnostics、early diagnostic-only `kput()`/`kputs()`、BootInfo ABI、framebuffer mapping API 和 direct-map 排除边界不变。

## 4. Documentation And OpenSpec Alignment

- [x] 4.1 更新 `docs/en` 和 `docs/zh` 的 console/framebuffer 文档，说明 dynamic framebuffer grid、full framebuffer clear、Legacy VGA 80x25 fallback 和非目标边界。
- [x] 4.2 更新 runtime smoke validation 文档，记录 QEMU/OVMF 图形验证应观察 full clear、dynamic columns/rows、cursor、scrollback 和 Legacy fallback。
- [x] 4.3 检查 roadmap、OpenSpec、headers 和 source comments，确保不宣称完整图形 terminal、字体缩放、ANSI/VT、termios、locale/shaping、多终端、完整 POSIX 或 UEFI runtime/storage parity。

## 5. Source-Level Tests

- [x] 5.1 增加或更新源码级 tests，覆盖 backend-reported grid、VGA fixed 80x25、framebuffer dynamic grid 计算和最大 grid clamp。
- [x] 5.2 增加或更新源码级 tests，覆盖 console state 使用 dynamic visible width/height 进行 wrap、viewport redraw、PageUp/PageDown、cursor 和 clear。
- [x] 5.3 增加或更新源码级 tests，覆盖 full framebuffer clear 的 mapping bounds、stride/height/bytes-per-pixel 校验，以及禁止 direct-map framebuffer 写入。
- [x] 5.4 所有 Python 相关测试通过 `uv run pytest ...` 执行；若 `uv` 不可用，记录 blocker、跳过原因和剩余风险。

## 6. Build And Runtime Validation

- [x] 6.1 运行 OpenSpec status/validate 检查，确认 proposal、design、specs 和 tasks 可被工具识别。
- [x] 6.2 运行默认 `xmake` 构建，确认 Legacy VGA fallback 和普通 bounded userland baseline 仍可构建。
- [x] 6.3 对新增或修改的 C++ 源/头文件运行尽量贴近 freestanding x86_64 C++17 配置的 clang 辅助检查，区分历史诊断、当前变更诊断和工具链配置缺口。
- [x] 6.4 对新增或修改的 C++ 源/头文件运行 clangd 辅助检查或记录不可用原因；clang/clangd 不替代 xmake cross-toolchain build。
- [x] 6.5 在 QEMU headless 可用时运行 Legacy/default fallback smoke，确认 ASCII/Unicode console 逻辑和非 framebuffer 路径未回归；不可用时记录缺失工具和剩余风险。
- [x] 6.6 在 QEMU + OVMF + framebuffer 图形验证能力可用时验证 full framebuffer clear、dynamic columns/rows、可见文本、软件光标和 scrollback viewport；不可用时记录 skipped/blocked 与剩余风险。
