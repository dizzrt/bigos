## MODIFIED Requirements

### Requirement: Framebuffer backend renders console-owned Unicode cells

BigOS SHALL allow the framebuffer text backend to render the upgraded runtime console cell model by consuming console-owned codepoint cells, width roles, and display attributes. The backend MUST remain a renderer only; UTF-8 decoding, ANSI/VT parsing, display-attribute state transitions, codepoint storage, double-width layout decisions, scrollback ownership, and viewport policy MUST stay in the runtime console state.

#### Scenario: 单宽 codepoint 按 glyph lookup 绘制

- **WHEN** the framebuffer backend receives a visible single-width console cell whose codepoint maps to an available glyph
- **THEN** BigOS MUST draw that glyph within the corresponding cell pixel rectangle using the cell's stored display attributes
- **AND** all pixel writes MUST remain within the mapped framebuffer range

#### Scenario: 双宽 leading cell 按两个 cell 宽度绘制

- **WHEN** the framebuffer backend receives a double-width leading cell whose codepoint maps to an available full-width glyph
- **THEN** BigOS MUST render the glyph across the deterministic two-cell pixel area reserved by console state using the leading cell's stored display attributes
- **AND** it MUST verify the two-cell range stays inside the configured visible grid and mapped framebuffer bounds before writing pixels

#### Scenario: 双宽 trailing cell 不独立查询 glyph

- **WHEN** the framebuffer backend receives a double-width trailing cell
- **THEN** BigOS MUST treat it as the occupied continuation of the preceding leading cell
- **AND** it MUST NOT independently query a glyph, advance a cursor, mutate scrollback state, mutate ANSI/VT parser state, or draw an unrelated visible character for that cell

#### Scenario: SGR 颜色映射为确定性像素颜色

- **WHEN** the framebuffer backend renders cells whose display attributes came from supported SGR sequences
- **THEN** BigOS MUST map supported foreground, background, bright foreground, and bright background attributes including `30-37`, `40-47`, `90-97`, and `100-107` to deterministic foreground and background pixel colors
- **AND** the mapping MUST NOT require firmware calls, hosted color libraries, filesystem access, dynamic palette loading, or userland cooperation

#### Scenario: 缺字或 unsupported codepoint 有确定性渲染

- **WHEN** the framebuffer backend receives a codepoint cell whose glyph lookup result is missing, unavailable, invalid, or unsupported
- **THEN** BigOS MUST first try to render the `U+FFFD` replacement glyph using the cell's stored display attributes
- **AND** if `U+FFFD` is unavailable, BigOS MUST render a deterministic question-mark glyph or blank according to the documented fallback policy
- **AND** glyph lookup failure MUST NOT corrupt adjacent cells, cursor rendering, scrollback state, ANSI/VT parser state, or terminal input state

### Requirement: Framebuffer Unicode rendering preserves backend boundaries

BigOS SHALL keep framebuffer Unicode and ANSI-colored rendering within the existing bounded runtime console backend boundary. Unicode rendering and SGR color rendering MUST NOT change framebuffer handoff, mapping, pixel format, boot, interrupt, syscall, page-table, or diagnostic-only output assumptions.

#### Scenario: Backend selection prerequisites remain required

- **WHEN** the framebuffer backend is selected for Unicode-capable and ANSI-colored console rendering
- **THEN** BigOS MUST still require validated framebuffer metadata, explicit writable device/MMIO mapping, supported pixel format, sufficient grid bounds, and usable glyph lookup view
- **AND** missing or invalid prerequisites MUST leave the Legacy VGA text and serial diagnostic fallback paths available

#### Scenario: Early diagnostics remain independent

- **WHEN** panic, early fault, fixed serial validation marker, or direct diagnostic-only output occurs before or outside runtime console readiness
- **THEN** those paths MAY continue using existing direct serial and VGA diagnostic APIs
- **AND** they MUST NOT depend on framebuffer Unicode rendering, ANSI/VT parser state, UTF-8 decoder state, terminal input, shell progress, or userland fd state

#### Scenario: 文档边界更新但不扩大为完整终端

- **WHEN** documentation, validation notes, tests, or source comments describe framebuffer Unicode cell rendering with ANSI/VT attributes
- **THEN** they MUST describe it as bounded rendering of console-owned Unicode/codepoint cells and display attributes to pixels
- **AND** they MUST NOT claim complete ANSI/VT support, termios, multiple terminals, complete Unicode terminal behavior, locale/shaping support, or complete graphical terminal behavior
