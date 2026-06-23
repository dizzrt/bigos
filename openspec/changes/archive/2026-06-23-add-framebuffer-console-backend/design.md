## Context

BigOS 当前 runtime console 已经拥有固定 80x25 cell 模型、256 行 scrollback、viewport 重绘、PageUp/PageDown/Home/End 导航，以及普通 stdout/stderr 到默认 console sink 的路径。该状态目前直接通过 `bigos::device::fill_video_text_cell()` 和 VGA text backend 渲染到 `0xb8000`，光标也依赖 VGA text 硬件光标。

前置能力已经提供两个输入边界：UEFI handoff 可暴露早期 framebuffer metadata，内核 glyph lookup 可暴露只读点阵字形查询。缺口在于 runtime console 尚没有可消费这些输入的 framebuffer renderer，也没有把 console 状态和具体显示 backend 拆开。该 change 横跨 boot handoff consumer、device/MMIO 映射、terminal console 状态、video backend 和 emulator 验证路径，但不改变 BootInfo ABI、link address、IDT/syscall vector、disk layout 或用户态 ABI。

目标数据流：

```text
ordinary console output / stdout / stderr
        |
        v
terminal console state: fixed cells, cursor, scrollback, viewport
        |
        v
console render backend interface
        |
        +--> framebuffer text backend: glyph lookup + MMIO framebuffer mapping
        |
        `--> VGA text backend fallback
```

## Goals / Non-Goals

**Goals:**

- 在 UEFI framebuffer、显式 device/MMIO 映射和有效 glyph lookup 都可用时，启用 framebuffer text console backend。
- 保持上层 console state、scrollback ring、viewport policy 和默认 terminal write path 可复用，避免为图形路径复制一套 terminal 状态。
- 用点阵 glyph 渲染当前 cell 字符，并实现软件光标显示、隐藏和移动。
- 让 clear、scrollback viewport redraw、自动上卷和历史视口导航都能通过 framebuffer backend 重绘。
- 在 framebuffer、映射或字体不可用时确定性回退到现有 VGA text/serial 路径。
- 增加源码级、构建级和 QEMU/OVMF 图形路径验证记录。

**Non-Goals:**

- 不实现 UTF-8 decoding、Unicode codepoint cell model、双宽 cell 占位或 CJK 排版；这些属于后续文本模型升级。
- 不实现 ANSI/VT100 escape parser、颜色属性状态机、framebuffer 加速、双缓冲、dirty rectangle 合并、多终端或完整 POSIX terminal。
- 不改变 BootInfo v2 section ABI、UEFI loader 字体路径、Legacy BIOS 启动路径、kernel link address、page-table layout、IDT/syscall vector、磁盘布局或用户态 syscall ABI。
- 不把 early diagnostic-only `kput()`/`kputs()` 强制纳入 framebuffer console；panic、早期 fault 和 serial marker 仍保持独立诊断边界。
- 不要求 Legacy BIOS 图形 backend、VBE、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI/Legacy storage parity。

## Decisions

1. **console state 与 render backend 分离。**

   - 决策：保留 `terminal::console_*` 作为唯一上层状态所有者，新增内部 renderer/backend 边界负责 `clear`、`draw_cell`、`set_cursor` 或等价整屏重绘操作。VGA text 和 framebuffer 都挂在这个边界之后。
   - 理由：scrollback、viewport、clear policy 和 cursor position 已经在 console state 中实现；复制状态会引入分叉和 off-by-one 风险。
   - 替代方案：新增独立 framebuffer console 状态。该方案能快速画字，但会让 VGA 与 framebuffer 的 scrollback 行为分裂，也会让 keyboard scrollback event 需要感知 backend。

2. **framebuffer backend 只在所有输入都有效时启用。**

   - 决策：启用条件同时要求有效 framebuffer metadata、保守映射成功、支持的像素格式和可用 glyph lookup view。任何条件失败都继续使用 VGA text fallback。
   - 理由：图形 console 是 UEFI 路径增强，不应破坏 Legacy baseline 或 headless smoke 的可运行性。
   - 替代方案：允许无字体时用矩形或空白占位。该方案会掩盖字体/renderer 集成问题，且不满足文本 console 的可见性目标。

3. **首版渲染现有 80x25 cell viewport。**

   - 决策：framebuffer backend 按 glyph/cell metrics 把当前 console 的 80x25 viewport 渲染到 framebuffer 的左上角或确定性 origin，超出 framebuffer 的区域拒绝启用或裁剪为不可用 fallback。
   - 理由：当前 console/scrollback 状态固定为 80x25；本 change 的目标是复用该状态，而不是同时引入可变行列布局。
   - 替代方案：根据 framebuffer 尺寸动态计算列数和行数。该方案更接近完整图形终端，但会扩大 console state、scrollback capacity、keyboard viewport 和测试矩阵，不适合当前变更边界。

4. **软件光标由 backend 以 cell 反色或覆盖方式表达。**

   - 决策：framebuffer backend 维护上一次光标 cell，重绘旧 cell 后再绘制新光标；VGA backend 继续使用硬件光标或已有接口。
   - 理由：framebuffer 没有硬件 text cursor；把软件光标封装在 backend 内能让上层 console 继续只表达 cell 坐标。
   - 替代方案：把光标作为特殊字符写入 console state。该方案会污染 scrollback 历史，且会让读取历史时出现伪字符。

5. **glyph renderer 只消费当前 byte/cell 字符，不声明 Unicode 支持。**

   - 决策：renderer 将现有 `char` cell 扩展为无符号 codepoint 查询 ASCII/当前 byte 范围字形；缺失字形使用确定性 replacement 或空白策略，并记录验证边界。
   - 理由：UTF-8 decoding 和双宽 cell 是后续能力。当前 renderer 需要先把已存在的 console 输出从 VGA text 搬到 framebuffer。
   - 替代方案：一次性把 console cell 改为 codepoint。该方案会把文本模型升级、CJK 宽度和 renderer 集成绑在一起，风险过大。

## Risks / Trade-offs

- [Risk] framebuffer 写入通过错误映射或普通 RAM alias 访问，可能破坏内存或触发不可诊断 fault。Mitigation: 只通过显式 device/MMIO mapping API 获取 framebuffer VA，并保留 framebuffer 物理范围 reserved 语义。
- [Risk] renderer 每次字符输出触发整屏重绘，性能较差。Mitigation: 初版允许整屏有界重绘以降低状态复杂度，后续可在 backend 内增加 dirty cell 优化，但不得改变 console state 语义。
- [Risk] framebuffer 尺寸不足以容纳 80x25 glyph grid。Mitigation: 初始化时检查 `80 * cell_width` 和 `25 * cell_height` 是否落在 framebuffer 几何内；不满足则回退。
- [Risk] 软件光标和 scrollback 重绘顺序错误会把光标残影写入历史视口。Mitigation: 光标不进入 console cell state；backend 在重绘 viewport 时先绘制 cell，再按当前可见 cursor 坐标覆盖。
- [Risk] QEMU/OVMF 图形验证依赖本地工具链和图形能力。Mitigation: 记录缺失工具为 skipped/blocked；同时补充源码级检查和普通构建验证，确保 fallback 与边界不被破坏。

## Migration Plan

1. 定义 console render backend 边界，并把现有 VGA text 渲染适配到该边界。
2. 新增 framebuffer backend 初始化：校验 handoff、映射 framebuffer、校验 glyph lookup、计算 80x25 grid 是否可见。
3. 实现 glyph 绘制、cell 清除、整屏 viewport 重绘和软件光标。
4. 将 `console.cc` 的 `render_viewport()` 和 cursor 更新改为调用选中的 backend，保持 scrollback 状态不变。
5. 保留 early diagnostics 和 fallback 路径，验证无 framebuffer/无字体/映射失败时 VGA text baseline 仍可运行。
6. 补充 OpenSpec specs、源码级 tests、构建验证和 QEMU/OVMF 图形验证记录。

回滚策略：保留 backend interface 时可把默认选择固定回 VGA text；若 backend interface 本身引入问题，可回退 `console.cc` 到现有 `bigos::device::*video_text*` 调用，同时保留 framebuffer handoff 和 glyph lookup 前置能力不变。

## Open Questions

- 首版软件光标采用反色、固定颜色覆盖还是下划线样式，需要在实现时按 framebuffer 像素格式和现有颜色语义选择最小可验证方案。
- 若 framebuffer 像素格式不是首版支持格式，fallback 是静默回退还是串口诊断一行，需要实现时结合已有 early/runtime diagnostic 边界确定。
