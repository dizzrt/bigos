## MODIFIED Requirements

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
