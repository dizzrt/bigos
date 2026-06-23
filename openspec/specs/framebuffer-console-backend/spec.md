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
