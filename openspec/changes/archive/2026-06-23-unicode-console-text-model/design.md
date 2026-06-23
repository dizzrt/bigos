## Context

BigOS 当前 runtime console 已经拥有固定 80x25 viewport、256 行 scrollback、PageUp/PageDown/Home/End 导航、clear policy、VGA text backend 和 framebuffer text backend。普通 kernel console 输出、默认 shell stdout/stderr 和用户程序输出都进入同一 console sink，但内部 cell 仍是 `char`，`console_write()` 逐 byte 调用 `console_put()`，framebuffer renderer 也只把 byte 扩展成 ASCII/当前 byte 范围 codepoint 查询 glyph。

前置能力已经完成两件事：kernel glyph lookup 可以按 Unicode codepoint 返回半宽/全宽 glyph metadata，framebuffer backend 可以把 console-owned viewport 渲染到线性 framebuffer。缺口在 text model 层：console 还不能把 UTF-8 byte stream 转换成 codepoint，也没有双宽 cell 的 trailing 占位、光标前进、scrollback/backspace/viewport 行为。

目标数据流：

```text
ordinary console byte stream / stdout / stderr
        |
        v
bounded UTF-8 decoder: codepoint or replacement
        |
        v
console-owned Unicode cells: codepoint + width role + color
        |
        v
runtime console state: cursor, scrollback, viewport, clear/backspace policy
        |
        v
render backend
        +--> framebuffer: glyph lookup by codepoint, half/full width drawing
        `--> Legacy VGA text: deterministic ASCII/degraded glyph display
```

## Goals / Non-Goals

**Goals:**

- 在默认 runtime console 输出路径中实现有界 UTF-8 解码，支持跨 `console_write()` 调用保留不完整序列状态。
- 将 console cell 从 byte/`char` 升级为 codepoint cell，并记录单宽、双宽 leading、双宽 trailing、空白或 replacement 等最小布局角色。
- 使用 kernel glyph lookup 的 width class 决定首版 cell 宽度：半宽 glyph 占一个 cell，全宽 glyph 占两个 cell；不可查询或不支持的 codepoint 优先使用 `U+FFFD` replacement glyph。
- 让换行、严格 4 列 tab stop、carriage return、backspace、自动上卷、clear、scrollback retention 和 viewport redraw 都按 codepoint cell 与双宽 cell 语义保持确定性。
- 让 framebuffer backend 按 codepoint 查询 glyph 并绘制单宽或双宽 cell；让 VGA text backend 对非 ASCII codepoint 做确定性降级。
- 补充源码级测试、构建验证和 QEMU/OVMF 图形验证记录，区分 Unicode display、Legacy fallback 和缺失工具风险。

**Non-Goals:**

- 不实现 ANSI/VT100 escape parser、颜色属性状态机、termios、多终端、伪终端、完整 POSIX terminal、background read/write control 或完整 job-control 语义。
- 不实现 locale、Unicode normalization、combining mark、grapheme cluster、emoji ZWJ、ambiguous-width 策略、东亚宽度完整表、字体 fallback 链或 shaping。
- 不改变 BootInfo ABI、UEFI loader 字体路径、framebuffer 映射策略、kernel link address、page-table layout、CR3 切换规则、IDT/syscall vector、磁盘布局或用户态 syscall ABI。
- 不把 early diagnostic-only `kput()`/`kputs()`、panic/fault diagnostics 或 COM1 serial marker 强制纳入 Unicode console。
- 不要求 Legacy BIOS 图形 backend、VBE、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI/Legacy storage parity。

## Decisions

1. **在 console sink 内部维护流式 UTF-8 decoder。**

   - 决策：`console_write()` 继续接收 `char *` byte stream，但 console state 新增一个小型 decoder 状态，按 byte 产生 codepoint 或 U+FFFD replacement。遇到非法 continuation、overlong、surrogate、越界 codepoint 或序列超长时，必须确定性输出 replacement 并重新同步。
   - 理由：用户态和内核普通输出 API 都已经是 byte stream；在 sink 内解码可以避免扩大 syscall/fd ABI，也能处理一次 write 被拆成多次 `console_write()` 的情况。
   - 替代方案：要求调用方传 codepoint。该方案会扩大 public API 和用户态 ABI，不适合当前有界终端路径。

2. **console state 拥有 codepoint cell 与 width role。**

   - 决策：用新的内部 cell 替代 `ConsoleRenderCell::ch` 的 byte 语义，至少记录 `codepoint`、`color` 和 cell role。双宽字符写入 leading cell 后，在后一格写入 trailing cell；trailing cell 不独立渲染 glyph，只用于 cursor/backspace/viewport 重绘保持布局一致。
   - 理由：双宽布局必须由 console state 统一拥有，否则 framebuffer 和 VGA backend 会各自决定宽度，scrollback 历史会分裂。
   - 替代方案：只在 framebuffer renderer 临时扩展宽度。该方案不能正确处理 cursor、backspace、line wrap 和 scrollback redraw。

3. **宽度策略首版以 glyph lookup width class 为准。**

   - 决策：如果 glyph lookup 返回 `Full`，console 尝试写入两个 cell；返回 `Half` 或 ASCII control-handled printable 则写入一个 cell；lookup unavailable/not-found/unknown 时优先使用 `U+FFFD` replacement codepoint 并重新查询 glyph/width，`U+FFFD` 不可用时再确定性降级为 `?` 或 blank。
   - 理由：前置 font asset 已经记录半宽/全宽类别，复用该边界能避免在 kernel 内维护大规模 Unicode width table。
   - 替代方案：内置 East Asian Width range 表。该方案更接近完整 Unicode terminal policy，代码和测试矩阵都更大，且会和 glyph asset coverage 产生双源不一致。

4. **双宽字符在行尾不能拆分。**

   - 决策：当双宽 codepoint 需要写入但当前列只剩一个 cell 时，先填充或清理当前单元为确定性 blank，再换行写入双宽字符；如果一整行宽度不足以容纳则退化为 replacement/blank。
   - 理由：拆分双宽字符会破坏 viewport redraw 和 backend bounds checking，也会让 trailing cell 无法定义。
   - 替代方案：在最后一列绘制截断 glyph。该方案会使 framebuffer 与 VGA fallback 行为不一致，且不适合 scrollback 中的逻辑 cell 模型。

5. **Tab 严格推进到 4 列 tab stop。**

   - 决策：Tab 不再简单等价为“输出 4 个空格”，而是推进到下一个 4 列边界；推进过程写入普通空白 cell，并在遇到双宽 trailing、行尾或需要 wrapping 时遵循同一 codepoint cell 布局规则。
   - 理由：tab stop 是更稳定的列对齐语义，能够让 ASCII 与 CJK 混排时的缩进结果可预测，也方便把测试写成基于列位置的断言。
   - 替代方案：保持现有“输出 4 个空格”。该方案实现更小，但在不同起始列输出 Tab 会产生不一致的列对齐，不适合作为升级后的 Unicode text model 语义。

6. **backspace 按逻辑字符删除，必要时跨 trailing/leading cell。**

   - 决策：backspace 若位于双宽 trailing 后方，需要删除 leading 与 trailing 两个 cell；若光标落在 trailing cell，先回到 leading，再清理整个双宽字符；普通单宽字符只清理一个 cell。行首跨行行为保持现有有界规则。
   - 理由：用户可见的删除单位应该匹配已写入的逻辑字符，否则会留下半个 glyph 或占位污染。
   - 替代方案：继续按单 cell 删除。该方案会在 CJK 输出后留下不可渲染的 trailing cell。

7. **render backend 接收 codepoint cell，但 Legacy VGA text 确定性降级。**

   - 决策：更新 internal render cell 结构和 backend 接口。Framebuffer backend 对 leading/single cell 查询 glyph；双宽 leading 按两个 cell pixel 宽度绘制，trailing cell 跳过或清背景。Framebuffer 缺字优先绘制 `U+FFFD` glyph，若 `U+FFFD` 也不可用则降级为 `?` 或 blank。VGA backend 对 ASCII printable 直接显示，对非 ASCII、missing glyph、trailing cell 和 unsupported cell role 使用固定 `?` 或 blank 降级。
   - 理由：framebuffer 是 Unicode 显示路径；Legacy VGA text 硬件无法表达 CJK glyph，但 fallback 必须稳定可读且不破坏 scrollback。
   - 替代方案：在 VGA text 后端尝试 codepage 映射。该方案需要额外字符集策略，且不满足“确定性降级”优先目标。

## Risks / Trade-offs

- [Risk] UTF-8 decoder 对非法序列处理不一致，导致输出错位或无限等待。Mitigation: decoder 必须有固定最大序列长度、明确 reset 规则和源码级测试覆盖 invalid/partial/overlong/surrogate/out-of-range cases。
- [Risk] 双宽 wrapping、backspace 和 scrollback ring 的边界组合容易产生 off-by-one。Mitigation: 把 cell role 作为状态机显式字段，并增加针对行尾、行首、ring rollover、PageUp/PageDown redraw 的测试。
- [Risk] 依赖 glyph lookup width class 会让缺字 codepoint 的宽度策略保守。Mitigation: 首版缺字优先使用 `U+FFFD` replacement glyph；如果 `U+FFFD` 不可用，再降级为 `?` 或 blank，并且不宣称完整 Unicode width policy。
- [Risk] 严格 4 列 tab stop 在双宽 trailing 和行尾附近更容易暴露边界错误。Mitigation: Tab 推进只写入普通空白 cell，并通过针对不同起始列、双宽相邻位置和行尾 wrapping 的源码级测试固定行为。
- [Risk] framebuffer renderer 绘制双宽 glyph 时可能越界写 pixel。Mitigation: 仍通过 backend cell/grid bounds 检查，双宽 leading 必须验证两个 cell pixel range，trailing cell 不独立绘制 glyph。
- [Risk] Legacy VGA fallback 可见内容降级后不等价于 framebuffer。Mitigation: spec 明确 Legacy 只需确定性降级显示，验证关注不崩溃、不破坏 cursor/scrollback 和 ASCII baseline。
- [Risk] 图形化 Unicode 验证依赖本地 QEMU/OVMF/截图或人工观察。Mitigation: 自动验证优先覆盖源码级和构建级行为；图形验证不可用时记录 skipped/blocked 与剩余风险。

## Migration Plan

1. 扩展 internal console/render cell 类型，加入 `codepoint` 和 cell role，同时保留颜色语义和固定 80x25/256-line bounds。
2. 在 console sink 中实现有界 UTF-8 decoder，并把 control byte handling 与 printable codepoint handling 分离。
3. 实现 codepoint 写入、宽度决策、双宽 trailing cell、line wrap、严格 4 列 tab stop、backspace、clear、scrollback 和 viewport redraw 行为。
4. 更新 framebuffer backend 按 codepoint 和 width role 查询/绘制 glyph，更新 VGA backend 的 ASCII passthrough 与非 ASCII deterministic degradation。
5. 更新文档、OpenSpec specs、源码级 tests、默认构建和 emulator 验证记录。

回滚策略：如果 Unicode cell model 引入启动或 console 可用性回归，可把 backend 选择和 console cell 写入临时固定回 byte/ASCII 路径，同时保留 glyph lookup 与 framebuffer backend 前置能力不变；若 internal cell ABI 变更影响较大，回退 `ConsoleRenderCell` 到 char 版本并保留 proposal/spec 作为未实现计划。

## Open Questions

- 当前无待定设计问题；首版缺字策略固定为优先 `U+FFFD` glyph，Tab 策略固定为严格推进到 4 列 tab stop。
