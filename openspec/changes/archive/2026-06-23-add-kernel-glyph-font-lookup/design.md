## Context

当前 UEFI framebuffer handoff 已经完成：构建路径会从随附 Unifont HEX 生成 `/boot/fonts/unifont.bin`，UEFI loader 会加载该文件并通过 BootInfo v2 font asset metadata 把地址、大小、格式版本和度量传给内核。这个阶段刻意不执行 glyph lookup；现有 payload 只是一个 header 加原始 HEX 文本，后续 framebuffer console 若直接消费它，会把复杂解析、字符串处理和缺字策略带入内核启动路径。

本 change 将字体资产边界前移到构建期：把源字体转换成内核可以在 freestanding 环境下只读查询的紧凑二进制格式。UEFI loader 继续保持“加载与描述”职责，内核负责校验 payload layout 并暴露 bounded glyph lookup view，framebuffer renderer 和 UTF-8/cell 模型仍由后续 change 实现。

数据流如下：

```text
assets/fonts/unifont_all-17.0.04.hex
        |
        v
构建期转换器 -> build/assets/fonts/unifont.bin -> ESP:/boot/fonts/unifont.bin
        |                                               |
        |                                               v
        |                                      UEFI loader 加载并写入 BootInfo metadata
        |                                               |
        v                                               v
格式/覆盖测试                              kernel early handoff 保存 font asset view
                                                        |
                                                        v
                                      kernel glyph lookup 校验并暴露只读查询接口
```

## Goals / Non-Goals

**Goals:**

- 生成版本化、紧凑、可边界校验的 glyph lookup payload，覆盖 ASCII/半宽和 CJK/全宽字形输入范围。
- 提供内核侧只读查询接口，按 codepoint 返回 glyph bitmap、glyph/cell 度量和宽度类别。
- 对 payload header、索引表、范围表、bitmap 偏移和大小做确定性校验，失败时保留现有 serial/VGA fallback。
- 让 UEFI loader 保持简单：只校验基本 header、加载 payload、写 BootInfo font metadata，不执行 codepoint lookup。
- 保持现有 BootInfo required section、kernel entry ABI、内存布局、IDT/syscall vector、CR3 切换和用户态 ABI 不变。

**Non-Goals:**

- 不实现 framebuffer glyph renderer、软件光标、framebuffer 滚动或图形 console backend。
- 不实现 UTF-8 解码、codepoint cell buffer、双宽 cell 排版策略或 Legacy text backend 的非 ASCII 降级显示。
- 不引入运行期字体文件解析、动态字体加载、压缩解压、字体 fallback 链或 hosted text shaping。
- 不改变 Legacy BIOS 启动路径，不增加 VBE/BIOS 图形 backend。
- 不实现 Secure Boot、ACPI table handoff、UEFI Runtime Services、SMP、完整 POSIX 或新存储/设备驱动。

## Decisions

### Decision: 使用构建期离线转换，而不是内核运行期解析 HEX

构建期转换器读取随附 Unifont HEX，生成二进制 glyph lookup payload。内核只做结构校验和只读查询，不解析 HEX 文本。

理由：
- HEX 解析需要字符串扫描、十六进制转换和错误恢复，不适合早期 freestanding kernel 路径。
- 转换期可以做覆盖率检查、重复 codepoint 检测、bitmap 长度归一化和格式一致性验证。
- 后续 framebuffer renderer 需要低延迟、无分配、确定性 lookup。

备选方案是在内核启动时解析 Unifont HEX。否决原因是会扩大内核字符串解析面，增加启动期失败模式，并把本可在构建期发现的字体数据错误推迟到运行期。

### Decision: payload 使用范围索引加连续 glyph records

字体 payload 应包含固定 header、范围表、glyph record 表和 bitmap 数据区。范围表描述 codepoint 区间、宽度类别和 records 起始位置；glyph record 记录 codepoint、bitmap offset、bitmap size 和度量/flag。lookup 先按范围定位，再在 bounded records 中查找。

理由：
- 范围表比完整 Unicode 平铺表节省空间，也比只使用线性全表更容易做 bounds 校验。
- record 与 bitmap 分离让内核能在不复制 bitmap 的情况下返回只读切片。
- 半宽和全宽字形可用 width class 明确表示，避免 renderer 反向猜测。

备选方案是建立 codepoint 到 bitmap 的完整直接索引。否决原因是 Unicode 空间稀疏，直接索引浪费内存并增加 ESP payload 体积。另一个备选是压缩 bitmap，否决原因是需要运行期解压或复杂随机访问，不适合作为首版内核 lookup。

### Decision: 首版只收录 Unifont 的 8x16 和 16x16 bitmap

首版 glyph lookup 只接受 Unifont HEX 中可归一为 8x16 半宽或 16x16 全宽的 bitmap。其它尺寸的 glyph 不进入首版 payload，也不通过 flags 预留为“暂不可消费”记录；后续若需要其它尺寸，应以新版 format version 或显式兼容扩展处理。

理由：
- framebuffer text backend 的首个消费方只需要稳定的半宽/全宽单元输入。
- 拒绝未消费尺寸比把不可渲染 glyph 放入 payload 更容易验证，也避免 renderer 未来误用。
- 首版格式和测试矩阵保持小而明确。

备选方案是在 payload 中保留其它尺寸并通过 flags 标记为不可消费。否决原因是会扩大内核校验面，并让“asset 中存在”和“renderer 可消费”之间出现额外状态。

### Decision: width class 由 Unifont HEX bitmap 宽度推断

构建期 width class 以前述 bitmap 宽度为准：8x16 归类为半宽，16x16 归类为全宽。该分类只描述字体资产 lookup 属性，不定义终端 cell 策略；后续 UTF-8/text cell change 可基于 Unicode 规则、环境策略或 fallback 行为决定最终 cell width。

理由：
- 字体转换器已经必须解析 bitmap 宽度，直接由宽度推断可避免维护另一份 codepoint range 表。
- 资产 lookup 层的职责是提供 glyph 数据和声明度量，不应提前固化 terminal wcwidth 语义。
- 后续 cell model 可以独立演进，不需要改变首版 glyph bitmap 数据本身。

备选方案是维护独立 codepoint width range 表。否决原因是它更接近终端文本模型策略，容易与后续 Unicode cell handling 重叠并产生双源不一致。

### Decision: 缺字语义由 lookup 层确定，但不负责文本降级策略

lookup API 对不存在或不可用 codepoint 返回 deterministic missing-glyph 状态；是否显示替代字形、问号、空白或 Legacy 降级，由后续 console/text model 决定。

理由：
- 本 change 的边界是“可查询字形资产”，不是文本模型或 renderer 策略。
- 缺字状态比直接替换更利于后续 UTF-8/cell 层做一致策略。
- Legacy text backend 仍可保持当前 ASCII/VGA 行为。

备选方案是在 lookup 层强制返回 fallback glyph。否决原因是会提前固化渲染策略，并混淆“字体缺字”和“显示降级”两类问题。

### Decision: UEFI loader 只做基本格式门禁

UEFI loader 继续从 ESP 加载 `/boot/fonts/unifont.bin`，校验 magic、header size、format version、基本度量和 byte size，并写入 BootInfo font metadata。完整范围表和 bitmap bounds 校验在内核 glyph lookup 初始化时完成。

理由：
- loader 环境更脆弱，保持职责窄有利于 boot path 稳定。
- 内核拥有最终消费者语义，适合集中校验 renderer 未来依赖的 invariants。
- 与 M7.1 已建立的 handoff 角色一致。

备选方案是在 loader 中完成完整 glyph table 校验。否决原因是会扩大 UEFI loader 代码面，并让 bootloader 与 kernel 消费语义重复维护。

## Risks / Trade-offs

- [Risk] 字体 payload 格式后续可能需要支持更多度量或压缩。-> Mitigation: header 使用 version、header size、flags 和 section/table size 字段；首版只承诺当前 uncompressed lookup 格式。
- [Risk] Unifont 覆盖范围较大，ESP payload 和内核校验成本增加。-> Mitigation: 构建期记录 glyph count 和 payload size，内核校验保持线性一次性边界检查，lookup 路径避免动态分配。
- [Risk] 半宽/全宽分类与后续 Unicode cell model 可能需要更细规则。-> Mitigation: 本 change 只记录字体资产声明的 width class，不定义终端排版策略；M7.4 可在 text model 层调整 cell width 决策。
- [Risk] loader、BootInfo metadata 和内核 payload header 的 format version 不一致会导致启动路径误判。-> Mitigation: loader 和内核均检查 magic/version/size；失败时 font view 标记 unavailable，现有 serial/VGA fallback 保持可用。
- [Risk] 构建期脚本错误可能生成排序或范围重叠的表。-> Mitigation: 转换器必须拒绝重复 codepoint、乱序范围、重叠 bitmap、越界 offset 和不匹配的 glyph count，并通过 pytest 覆盖。

## Migration Plan

1. 保留现有源字体路径和 ESP 运行时路径，替换 `build/assets/fonts/unifont.bin` 的内部 payload layout。
2. 更新 UEFI loader 和 BootInfo font format 常量，使其识别新版 glyph lookup asset。
3. 增加内核 glyph lookup 校验与只读视图；校验失败时不阻塞普通 boot baseline。
4. 更新 docs/en 与 docs/zh 的 UEFI/framebuffer 字体资产说明，继续明确 renderer 与 Unicode display 未完成。
5. 验证通过后，路线图中对应能力可标记为完成；后续 renderer change 只依赖 lookup API，不再解析字体文件。

回滚策略：若新版 payload 导致 UEFI 字体加载或内核校验问题，可回退到 font metadata unavailable/fallback 行为；不得影响 required BootInfo section、bounded userland baseline 或 Legacy BIOS 路径。

## Open Questions

- 无。
