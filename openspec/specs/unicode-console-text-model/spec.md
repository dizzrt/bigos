## Purpose

Define the bounded Unicode text model for the default BigOS runtime console.
This capability covers UTF-8 decoding, codepoint cells, single-width and
double-width layout, deterministic fallback, and backend-facing cell roles while
preserving fixed-capacity console state and early diagnostic independence.
## Requirements
### Requirement: 控制台输出执行有界 UTF-8 解码

BigOS SHALL decode ordinary runtime console output byte streams as UTF-8 before storing visible text in console cells. The decoder MUST be bounded, freestanding-safe, deterministic for invalid input, independent from early diagnostic-only output paths, and sequenced after ANSI/VT parser classification so escape sequence bytes are not accidentally stored as ordinary Unicode text.

#### Scenario: 有效 UTF-8 序列生成 codepoint

- **WHEN** ordinary console output receives a valid UTF-8 byte sequence for a supported Unicode scalar value while the ANSI/VT parser is in ordinary text state
- **THEN** BigOS MUST decode the sequence into the corresponding codepoint before updating runtime console cells
- **AND** the behavior MUST NOT require hosted locale services, dynamic allocation, filesystem access, or userland cooperation

#### Scenario: 不完整序列可跨 write 保留

- **WHEN** a valid multi-byte UTF-8 sequence is split across bounded calls into the default console output sink
- **THEN** BigOS MUST retain enough decoder state to complete the codepoint when the remaining bytes arrive
- **AND** the retained state MUST remain fixed-size and console-owned

#### Scenario: Escape parser 优先于 UTF-8 普通文本解码

- **WHEN** console output receives an escape introducer byte for the supported ANSI/VT parser
- **THEN** BigOS MUST route subsequent escape-sequence bytes through the bounded parser rather than storing them as ordinary printable Unicode cells
- **AND** once the parser completes or recovers, subsequent ordinary bytes MUST resume UTF-8 decoding deterministically

#### Scenario: 非法序列确定性替换

- **WHEN** console output receives an invalid UTF-8 sequence, overlong encoding, surrogate codepoint, out-of-range codepoint, stray continuation byte, or sequence that exceeds the supported maximum length while in ordinary text state
- **THEN** BigOS MUST emit a deterministic replacement codepoint or deterministic fallback cell
- **AND** the decoder MUST reset or resynchronize without corrupting subsequent valid output, scrollback state, ANSI parser state, terminal input, or render backend state

#### Scenario: Early diagnostics 不依赖 UTF-8 decoder

- **WHEN** panic, early fault diagnostics, direct `kput()`/`kputs()`, or fixed serial validation markers emit output before or outside runtime console readiness
- **THEN** those paths MAY continue using existing direct VGA/COM1 diagnostic behavior
- **AND** they MUST NOT depend on the runtime console UTF-8 decoder state or ANSI/VT parser state

### Requirement: 控制台 cell 使用 Unicode codepoint 和显式宽度角色

BigOS SHALL store ordinary runtime console text as bounded console cells that record the visible codepoint, display attributes, and cell role needed for single-width and double-width layout. Console-owned state MUST remain fixed-capacity and MUST NOT introduce dynamic scrollback growth.

#### Scenario: 单宽 codepoint 占用一个 cell

- **WHEN** a decoded codepoint is classified as single-width for the runtime console
- **THEN** BigOS MUST store it in one visible console cell with deterministic display attributes and role metadata
- **AND** cursor advancement, viewport redraw, and scrollback retention MUST treat it as one cell

#### Scenario: 双宽 codepoint 占用 leading 和 trailing cells

- **WHEN** a decoded codepoint is classified as double-width for the runtime console and two cells are available on the current line
- **THEN** BigOS MUST store a leading cell containing the codepoint and display attributes plus a trailing cell representing the occupied second cell
- **AND** render backends MUST be able to distinguish the trailing cell from an independently printable character

#### Scenario: 双宽 codepoint 不在行尾拆分

- **WHEN** a double-width codepoint would start at the final cell of the current console line
- **THEN** BigOS MUST avoid splitting the codepoint across the line boundary by applying a deterministic blank/fill and wrapping policy before storing the codepoint
- **AND** the behavior MUST preserve valid cursor position, display attributes, scrollback history, and viewport redraw semantics

#### Scenario: SGR 属性只影响后续 cell

- **WHEN** the ANSI/VT parser applies a supported SGR attribute change
- **THEN** BigOS MUST update console-owned current display attributes
- **AND** existing visible cells MUST keep their stored attributes unless a later erase, rewrite, or clear operation explicitly replaces those cells

#### Scenario: 固定容量不变

- **WHEN** Unicode codepoint cells and display attributes are used by the runtime console
- **THEN** BigOS MUST preserve the bounded visible grid and fixed scrollback capacity semantics
- **AND** it MUST NOT allocate unbounded history, write console history to the filesystem, or expose a new user-visible terminal ABI

### Requirement: 宽度策略由有界 glyph lookup 和 fallback 决定

BigOS SHALL use bounded kernel glyph lookup metadata to classify runtime console codepoints as single-width or double-width when possible. Missing, unsupported, or unavailable glyph metadata MUST prefer `U+FFFD` replacement glyph lookup before falling back to simpler deterministic degradation, and MUST NOT require a complete Unicode terminal width database.

#### Scenario: 半宽 glyph 产生单宽 cell

- **WHEN** glyph lookup reports a decoded codepoint as half-width
- **THEN** BigOS MUST classify the codepoint as single-width for console cell placement
- **AND** the renderer MUST NOT reserve a trailing cell for that codepoint

#### Scenario: 全宽 glyph 产生双宽 cell

- **WHEN** glyph lookup reports a decoded codepoint as full-width
- **THEN** BigOS MUST classify the codepoint as double-width for console cell placement
- **AND** the console state MUST reserve a trailing cell according to the double-width cell policy

#### Scenario: 缺字或不可用 lookup 有确定性策略

- **WHEN** glyph lookup is unavailable, returns missing-glyph/not-found, or reports an unsupported width class for a decoded non-control codepoint
- **THEN** BigOS MUST first try to use `U+FFFD` as the replacement codepoint for width classification and rendering
- **AND** if `U+FFFD` is unavailable, BigOS MUST use a deterministic question-mark, blank, or documented fallback codepoint policy
- **AND** the fallback MUST NOT corrupt adjacent cells, scrollback history, input state, or backend selection

#### Scenario: 不声明完整 Unicode 宽度策略

- **WHEN** documentation, specs, validation notes, headers, or source comments describe console width classification
- **THEN** they MUST describe it as a bounded runtime console display policy based on available glyph metadata and deterministic fallback
- **AND** they MUST NOT claim complete Unicode East Asian Width, grapheme cluster, combining mark, emoji, locale, or shaping support

### Requirement: 控制字符和编辑行为保持 codepoint-aware

BigOS SHALL keep newline, carriage return, tab, backspace, clear, automatic wrapping, automatic scrolling, and viewport navigation deterministic after the cell model changes to Unicode codepoints and double-width cells.

#### Scenario: 换行和 carriage return 保持确定性

- **WHEN** decoded console output contains newline or carriage return control bytes
- **THEN** BigOS MUST update the logical cursor and current line according to the existing bounded console policy
- **AND** the behavior MUST remain valid when the previous visible cell is single-width, double-width leading, or double-width trailing

#### Scenario: Tab 推进到 4 列 tab stop

- **WHEN** decoded console output contains a tab control byte
- **THEN** BigOS MUST advance the cursor to the next 4-column tab stop by writing deterministic blank cells as needed
- **AND** the behavior MUST respect double-width trailing cells, line wrapping, automatic scrolling, and the fixed visible grid bounds

#### Scenario: backspace 删除逻辑字符

- **WHEN** console output receives backspace after a single-width or double-width visible character
- **THEN** BigOS MUST clear the complete logical character representation from console cells
- **AND** it MUST NOT leave a visible half of a double-width glyph or an orphan trailing cell

#### Scenario: scrollback 和 viewport 重绘保持一致

- **WHEN** retained history containing single-width and double-width cells is redrawn through PageUp, PageDown, Home, End, clear, or bottom-follow output
- **THEN** BigOS MUST redraw the visible grid from console-owned cell state deterministically
- **AND** render backends MUST NOT maintain a second divergent Unicode layout history

### Requirement: Legacy text backend 对非 ASCII 确定性降级

BigOS SHALL preserve the Legacy VGA text backend as a fallback for ordinary runtime console output. Because VGA text mode cannot render arbitrary Unicode glyphs, non-ASCII codepoints and unsupported cell roles MUST degrade deterministically.

#### Scenario: ASCII printable 保持直接显示

- **WHEN** the Legacy VGA text backend renders a single-width printable ASCII codepoint
- **THEN** BigOS MUST display the corresponding VGA text character with the current bounded color semantics
- **AND** this behavior MUST remain compatible with existing ASCII shell prompts and simple user-program output

#### Scenario: 非 ASCII 显示为确定性 fallback

- **WHEN** the Legacy VGA text backend renders a non-ASCII codepoint, missing glyph, unsupported codepoint, or double-width leading cell
- **THEN** BigOS MUST display a deterministic replacement such as `?` or blank according to the documented backend policy
- **AND** it MUST preserve cursor position, scrollback retention, and viewport redraw consistency

#### Scenario: trailing cell 不显示独立字符

- **WHEN** the Legacy VGA text backend renders a double-width trailing cell
- **THEN** BigOS MUST render a deterministic blank or documented placeholder
- **AND** it MUST NOT treat the trailing cell as an independently printable codepoint

