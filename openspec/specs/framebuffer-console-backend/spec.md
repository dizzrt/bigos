## Purpose

Define the bounded framebuffer text console backend for BigOS runtime console
rendering. This capability describes validated framebuffer selection, glyph-cell
rendering, software cursor behavior, and scrollback viewport redraw while
preserving the existing VGA text fallback and early diagnostic boundaries.

## Requirements

### Requirement: Framebuffer console backend initializes only from validated inputs

BigOS SHALL enable the framebuffer text console backend only when validated framebuffer metadata, an explicit writable device/MMIO mapping, a supported pixel format, and a usable kernel glyph lookup view are all available. Failure to satisfy any prerequisite MUST leave the existing VGA text and serial diagnostic fallback paths available.

#### Scenario: Valid UEFI framebuffer and font enable backend

- **WHEN** the kernel has parsed valid framebuffer metadata, mapped the framebuffer through the explicit device/MMIO mapping path, and initialized a valid glyph lookup view
- **THEN** BigOS MUST be able to select the framebuffer console backend for ordinary runtime console rendering
- **AND** the selected backend MUST NOT require UEFI Runtime Services, filesystem access, dynamic font parsing, userland cooperation, or direct user access to framebuffer memory

#### Scenario: Missing or invalid prerequisites fall back

- **WHEN** framebuffer metadata is absent, invalid, unmappable, has an unsupported pixel format, or the glyph lookup view is unavailable
- **THEN** BigOS MUST keep ordinary runtime console output on the existing VGA text fallback when that fallback is available
- **AND** serial diagnostics and bounded boot validation MUST remain available for recording the failure or skipped graphics path

#### Scenario: Framebuffer geometry must contain the text grid

- **WHEN** framebuffer console initialization computes the pixel bounds for the current runtime console viewport
- **THEN** it MUST verify that the configured cell grid fits inside the framebuffer width, height, stride, and byte-size bounds before selecting the backend
- **AND** it MUST reject or skip framebuffer rendering rather than writing outside the described framebuffer range

### Requirement: Framebuffer backend renders console cells through glyph lookup

BigOS SHALL render ordinary runtime console cells to the framebuffer by looking up bounded glyph bitmap data and writing pixels within each cell. The renderer MUST treat the current `char`-based console cell model as the input boundary and MUST NOT claim UTF-8 decoding, Unicode codepoint cell storage, or double-width terminal layout.

#### Scenario: Printable cell draws glyph pixels

- **WHEN** the selected framebuffer backend receives a visible console cell whose character maps to an available glyph
- **THEN** it MUST draw that glyph within the cell's pixel rectangle using the cell metrics and framebuffer pixel format
- **AND** all written pixels MUST remain within the mapped framebuffer range

#### Scenario: Missing glyph has deterministic rendering

- **WHEN** the selected framebuffer backend receives a visible console cell whose character has no available glyph
- **THEN** it MUST render a deterministic replacement glyph or blank cell according to the documented fallback policy
- **AND** glyph lookup failure MUST NOT corrupt adjacent cells, scrollback state, or terminal input state

#### Scenario: Renderer does not upgrade text semantics

- **WHEN** documentation, validation notes, tests, or source comments describe framebuffer glyph rendering
- **THEN** they MUST describe it as rendering the current runtime console cells to pixels
- **AND** they MUST NOT describe UTF-8 decoding, CJK display, codepoint-based cells, double-width cell handling, ANSI/VT support, or complete Unicode terminal behavior as completed by this backend

### Requirement: Framebuffer backend provides software cursor semantics

BigOS SHALL provide a software cursor for the framebuffer console backend because a linear framebuffer has no VGA text hardware cursor. The cursor MUST be drawn from backend state and MUST NOT be stored as a character in console scrollback history.

#### Scenario: Cursor moves without leaving stale pixels

- **WHEN** the runtime console moves the cursor while the framebuffer backend is selected
- **THEN** BigOS MUST restore the old cursor cell rendering and draw the new cursor at the current visible cursor cell
- **AND** the cursor update MUST NOT write outside the current visible viewport or mutate retained scrollback cells

#### Scenario: Cursor follows visible bottom output

- **WHEN** the console viewport is following newest output and ordinary runtime console text advances the cursor
- **THEN** the framebuffer backend MUST display the software cursor at the corresponding visible cell after rendering the updated viewport
- **AND** the cursor MUST remain hidden or clamped deterministically when the current logical cursor line is outside the visible historical viewport

### Requirement: Framebuffer backend redraws scrollback viewport and scrolling

BigOS SHALL allow the existing console scrollback and viewport state to redraw the framebuffer backend deterministically. Automatic scrolling, clear-screen behavior, PageUp/PageDown/Home/End viewport navigation, and bottom-follow policy MUST remain owned by the runtime console state rather than by a separate framebuffer terminal state.

#### Scenario: Viewport redraw paints complete visible grid

- **WHEN** a console viewport redraw is requested while the framebuffer backend is selected
- **THEN** BigOS MUST repaint every visible cell in the configured text grid from console-owned state
- **AND** blank areas in the visible grid MUST be cleared deterministically before the software cursor is applied

#### Scenario: Automatic scroll uses existing console state

- **WHEN** ordinary runtime console output advances beyond the last visible row while the framebuffer backend is selected
- **THEN** the existing console state MUST update current line, scrollback retention, viewport position, and cursor position according to the documented runtime console policy
- **AND** the framebuffer backend MUST render the resulting viewport without maintaining a second scrollback history

#### Scenario: Historical viewport receives new output

- **WHEN** the framebuffer backend is displaying retained history and new runtime console output arrives
- **THEN** BigOS MUST preserve the existing deterministic history viewport policy
- **AND** PageDown or End MUST be able to return the visible framebuffer console to newest output without losing retained history

### Requirement: Framebuffer console preserves low-level boundaries

BigOS SHALL keep framebuffer console rendering as a bounded runtime display backend. It MUST NOT change early diagnostic-only output, interrupt vector assignments, syscall ABI, page-table layout, kernel link addresses, disk layout, or the Legacy BIOS fallback boot path.

#### Scenario: Early diagnostics remain independent

- **WHEN** panic, early fault, fixed serial validation marker, or diagnostic-only output occurs before or outside runtime console readiness
- **THEN** BigOS MAY continue using the existing direct serial and VGA diagnostic paths
- **AND** those paths MUST NOT depend on framebuffer console initialization, glyph lookup availability, terminal input, shell progress, or userland fd state

#### Scenario: Legacy path remains runnable

- **WHEN** BigOS boots through the Legacy BIOS path or through a configuration without valid framebuffer console prerequisites
- **THEN** ordinary runtime console output MUST remain available through the existing VGA text backend when that backend is present
- **AND** the fallback path MUST NOT require OVMF, GOP, framebuffer mapping, glyph rendering, or UEFI-specific disk packaging

### Requirement: Framebuffer backend renders console-owned Unicode cells

BigOS SHALL allow the framebuffer text backend to render the upgraded runtime console cell model by consuming console-owned codepoint cells and width roles. The backend MUST remain a renderer only; UTF-8 decoding, codepoint storage, double-width layout decisions, scrollback ownership, and viewport policy MUST stay in the runtime console state.

#### Scenario: 单宽 codepoint 按 glyph lookup 绘制

- **WHEN** the framebuffer backend receives a visible single-width console cell whose codepoint maps to an available glyph
- **THEN** BigOS MUST draw that glyph within the corresponding cell pixel rectangle
- **AND** all pixel writes MUST remain within the mapped framebuffer range

#### Scenario: 双宽 leading cell 按两个 cell 宽度绘制

- **WHEN** the framebuffer backend receives a double-width leading cell whose codepoint maps to an available full-width glyph
- **THEN** BigOS MUST render the glyph across the deterministic two-cell pixel area reserved by console state
- **AND** it MUST verify the two-cell range stays inside the configured visible grid and mapped framebuffer bounds before writing pixels

#### Scenario: 双宽 trailing cell 不独立查询 glyph

- **WHEN** the framebuffer backend receives a double-width trailing cell
- **THEN** BigOS MUST treat it as the occupied continuation of the preceding leading cell
- **AND** it MUST NOT independently query a glyph, advance a cursor, mutate scrollback state, or draw an unrelated visible character for that cell

#### Scenario: 缺字或 unsupported codepoint 有确定性渲染

- **WHEN** the framebuffer backend receives a codepoint cell whose glyph lookup result is missing, unavailable, invalid, or unsupported
- **THEN** BigOS MUST first try to render the `U+FFFD` replacement glyph
- **AND** if `U+FFFD` is unavailable, BigOS MUST render a deterministic question-mark glyph or blank according to the documented fallback policy
- **AND** glyph lookup failure MUST NOT corrupt adjacent cells, cursor rendering, scrollback state, or terminal input state

### Requirement: Framebuffer Unicode rendering preserves backend boundaries

BigOS SHALL keep framebuffer Unicode rendering within the existing bounded runtime console backend boundary. Unicode rendering MUST NOT change framebuffer handoff, mapping, pixel format, boot, interrupt, syscall, page-table, or diagnostic-only output assumptions.

#### Scenario: Backend selection prerequisites remain required

- **WHEN** the framebuffer backend is selected for Unicode-capable console rendering
- **THEN** BigOS MUST still require validated framebuffer metadata, explicit writable device/MMIO mapping, supported pixel format, sufficient grid bounds, and usable glyph lookup view
- **AND** missing or invalid prerequisites MUST leave the Legacy VGA text and serial diagnostic fallback paths available

#### Scenario: Early diagnostics remain independent

- **WHEN** panic, early fault, fixed serial validation marker, or direct diagnostic-only output occurs before or outside runtime console readiness
- **THEN** those paths MAY continue using existing direct serial and VGA diagnostic APIs
- **AND** they MUST NOT depend on framebuffer Unicode rendering, UTF-8 decoder state, terminal input, shell progress, or userland fd state

#### Scenario: 文档边界更新但不扩大为完整终端

- **WHEN** documentation, validation notes, tests, or source comments describe framebuffer Unicode cell rendering
- **THEN** they MUST describe it as bounded rendering of console-owned Unicode/codepoint cells to pixels
- **AND** they MUST NOT claim ANSI/VT support, termios, multiple terminals, complete Unicode terminal behavior, locale/shaping support, or complete graphical terminal behavior

### Requirement: Framebuffer backend exposes dynamic grid metrics

BigOS SHALL extend the framebuffer text backend so it exposes the visible grid dimensions computed from framebuffer geometry and glyph cell metrics. The backend MUST continue to validate framebuffer prerequisites before selection and MUST leave the VGA fallback available on failure.

#### Scenario: Backend reports computed grid

- **WHEN** the framebuffer backend is selected
- **THEN** BigOS MUST make the computed visible columns and rows available to the runtime console state
- **AND** those dimensions MUST reflect complete glyph cells that fit within framebuffer width, height, stride, and mapping bounds

#### Scenario: Geometry validation includes dynamic grid bounds

- **WHEN** framebuffer backend initialization computes the dynamic grid
- **THEN** it MUST validate the pixel range for the maximum visible grid area and full framebuffer clear range
- **AND** it MUST reject framebuffer rendering rather than write outside the described framebuffer range

#### Scenario: VGA backend remains a fixed-grid backend

- **WHEN** framebuffer prerequisites are missing or invalid
- **THEN** the selected VGA text backend MUST continue to expose the fixed 80x25 visible grid
- **AND** ordinary console output, early diagnostics, and bounded userland validation MUST remain available

### Requirement: Framebuffer backend clears full background before grid drawing

BigOS SHALL clear the framebuffer backend's full mapped background before drawing the visible text grid on backend selection and console clear. This behavior MUST NOT require firmware calls after kernel entry.

#### Scenario: Full clear precedes first viewport draw

- **WHEN** framebuffer console backend selection succeeds during terminal initialization
- **THEN** BigOS MUST clear the full mapped framebuffer background before the first visible viewport draw
- **AND** the clear MUST use kernel-owned MMIO writes rather than UEFI Runtime Services or firmware console calls

#### Scenario: Redraw remains grid-scoped after full clear

- **WHEN** ordinary output or viewport navigation triggers redraw after initialization
- **THEN** BigOS MAY redraw only the visible text grid cells required by console state
- **AND** it MUST NOT reintroduce firmware residue outside the grid
