# kernel-glyph-font-lookup Specification

## Purpose

定义内核 glyph lookup 字体资产能力边界：构建期生成版本化二进制 lookup payload，UEFI/BootInfo 只传递有界元数据，内核只暴露无分配、只读、按 Unicode codepoint 查询的 glyph view。该能力不包含 framebuffer glyph rendering、UTF-8 decoding、Unicode terminal cell policy、软件光标或 framebuffer scrollback。

## Requirements
### Requirement: 构建期生成内核 glyph lookup 字体资产

BigOS SHALL 在构建期把随附点阵字体转换为版本化的二进制 glyph lookup asset。该 asset MUST 包含固定 magic、format version、header size、glyph/cell 度量、glyph count、范围或索引表边界、bitmap 数据边界和 flags，使 freestanding kernel 能在不解析源字体文本的情况下执行只读字形查询。

#### Scenario: 源字体转换为二进制 lookup payload

- **WHEN** 构建路径准备 boot-time font asset
- **THEN** 它 MUST 从随附点阵字体源生成 `build/assets/fonts/unifont.bin`
- **AND** 生成的 payload MUST 不再只是 header 加原始 HEX 文本，而是包含可校验的 glyph lookup 表和 bitmap 数据区
- **AND** 该 payload MUST 保持可被打包到 ESP 的 `/boot/fonts/unifont.bin`

#### Scenario: 转换器拒绝不一致字体数据

- **WHEN** 字体源包含重复 codepoint、无法识别的 bitmap 长度、越界 bitmap 数据、非法 codepoint、重叠范围或与 header 不一致的 glyph count
- **THEN** 构建期转换 MUST 失败并报告确定性诊断
- **AND** 它 MUST NOT 生成会被 UEFI loader 或内核误认为有效的 glyph lookup asset

#### Scenario: payload layout 支持未来兼容检查

- **WHEN** 字体 asset header 被 UEFI loader 或 kernel parser 读取
- **THEN** header MUST 提供足够字段来区分 magic、format version、header size、payload byte size、glyph count、glyph metrics、cell metrics 和 table offsets
- **AND** 未支持的 format version MUST 被拒绝或标记为 font metadata unavailable

### Requirement: 内核暴露只读 glyph lookup view

BigOS SHALL 在内核中把有效 glyph lookup asset 暴露为只读 bounded view。该 view MUST 支持按 Unicode codepoint 查询字形 metadata 和 bitmap slice，MUST 不执行动态分配、文件系统访问、UEFI Runtime Services 调用或运行期源字体文本解析。

#### Scenario: 有效字形可按 codepoint 查询

- **WHEN** kernel glyph lookup view 已从有效 font asset 初始化，且 consumer 查询 asset 中存在的 codepoint
- **THEN** lookup MUST 返回该 codepoint 对应的 glyph bitmap 只读范围、glyph width、glyph height、cell width、cell height 和 width class
- **AND** 返回的 bitmap 范围 MUST 位于已校验 font asset byte range 内

#### Scenario: 缺失字形返回确定性状态

- **WHEN** consumer 查询 asset 中不存在、未覆盖或不支持的 codepoint
- **THEN** lookup MUST 返回确定性的 missing-glyph 或 not-found 状态
- **AND** lookup 层 MUST NOT 自行决定 framebuffer 渲染替代策略、Legacy text 降级策略或终端 cell 布局策略

#### Scenario: 无效 payload 不暴露 lookup view

- **WHEN** font asset metadata 存在但 payload header、范围表、glyph record、bitmap offset、alignment 或 byte size 校验失败
- **THEN** kernel MUST 不暴露可用 glyph lookup view
- **AND** serial diagnostics、VGA text fallback、memory initialization 和 bounded userland validation MUST 保持不依赖该字体 view

### Requirement: 字形覆盖记录半宽和全宽类别

BigOS SHALL 在 glyph lookup asset 中记录每个可用 glyph 的宽度类别，使后续 framebuffer console 能区分半宽与全宽字形输入。该宽度类别 MUST 表示字体资产声明的 glyph/cell 覆盖，不得被描述为完整 Unicode terminal cell policy。

#### Scenario: 只收录首版支持的 bitmap 尺寸

- **WHEN** 构建期转换器处理 Unifont HEX glyph
- **THEN** 它 MUST 只把 8x16 bitmap 收录为半宽 glyph，把 16x16 bitmap 收录为全宽 glyph
- **AND** 其它 bitmap 尺寸 MUST 被拒绝或跳过并产生确定性转换诊断，不能作为暂不可消费 glyph record 混入首版 payload

#### Scenario: 半宽字形具有稳定度量

- **WHEN** 构建期转换器处理半宽 glyph
- **THEN** 生成的 glyph record MUST 根据 8x16 bitmap 宽度标记半宽 width class
- **AND** 记录的 glyph/cell 度量 MUST 足以让后续 renderer 在单 cell 宽度内读取 bitmap

#### Scenario: 全宽字形具有稳定度量

- **WHEN** 构建期转换器处理全宽 glyph
- **THEN** 生成的 glyph record MUST 根据 16x16 bitmap 宽度标记全宽 width class
- **AND** 记录的 glyph/cell 度量 MUST 足以让后续 renderer 在双 cell 宽度语义下读取 bitmap

#### Scenario: 字体宽度不定义终端排版

- **WHEN** documentation、spec 或 validation notes 描述 glyph lookup width class
- **THEN** 它们 MUST 将该信息描述为字体资产 lookup 属性
- **AND** 它们 MUST NOT 宣称 UTF-8 decoding、codepoint cell model、双宽 cell 布局或 Unicode display 已完成
