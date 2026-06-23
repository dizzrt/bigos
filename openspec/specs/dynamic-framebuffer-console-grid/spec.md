## Purpose

Define the dynamic framebuffer console grid behavior for BigOS runtime console
rendering. This capability covers computing a bounded visible text grid from
validated framebuffer geometry, clearing the full mapped framebuffer background,
and using backend-provided dimensions for console viewport behavior while
preserving the fixed Legacy VGA fallback.

## Requirements

### Requirement: Framebuffer console computes a dynamic visible text grid

BigOS SHALL compute the framebuffer console visible text grid from validated framebuffer geometry and glyph cell metrics. The computed grid MUST use only complete cells, MUST remain within bounded kernel storage limits, and MUST NOT require dynamic allocation.

#### Scenario: Valid framebuffer determines columns and rows

- **WHEN** the framebuffer backend has validated framebuffer width, height, stride, byte size, pixel format, mapping, and glyph cell metrics
- **THEN** BigOS MUST compute visible columns from `framebuffer_width / cell_width`
- **AND** BigOS MUST compute visible rows from `framebuffer_height / cell_height`
- **AND** the resulting grid MUST be clamped to documented bounded maximum columns and rows

#### Scenario: Insufficient grid falls back

- **WHEN** the computed framebuffer grid cannot fit the minimum supported text viewport or violates bounded maximum/state assumptions
- **THEN** BigOS MUST reject framebuffer console selection or fall back to the existing VGA text backend when available
- **AND** it MUST NOT write outside the mapped framebuffer or allocate an unbounded console cell buffer

#### Scenario: Legacy VGA grid remains fixed

- **WHEN** BigOS uses the Legacy VGA text backend
- **THEN** the visible text grid MUST remain the fixed VGA-compatible 80x25 grid
- **AND** dynamic framebuffer grid calculation MUST NOT be required for the Legacy BIOS fallback path

### Requirement: Framebuffer backend clears the full mapped framebuffer

BigOS SHALL clear the full validated mapped framebuffer to the console background color when the framebuffer backend is selected and when the supported console clear path runs. Full clear MUST be bounded to the mapped framebuffer byte range.

#### Scenario: Backend selection clears firmware residue

- **WHEN** the framebuffer backend is selected after successful probe
- **THEN** BigOS MUST clear the full mapped framebuffer background before ordinary console text is rendered
- **AND** firmware logos or previous GOP pixels outside the text grid MUST NOT remain visible because of untouched framebuffer areas

#### Scenario: Console clear covers entire framebuffer

- **WHEN** the supported runtime console clear path executes while the framebuffer backend is selected
- **THEN** BigOS MUST clear the full mapped framebuffer background
- **AND** it MUST reset the console-owned viewport and cursor according to the documented console clear policy

#### Scenario: Full clear stays within verified byte bounds

- **WHEN** full framebuffer clear writes pixels
- **THEN** every write MUST be bounded by the validated framebuffer mapping length, stride, height, and bytes-per-pixel values
- **AND** integer overflow or inconsistent geometry MUST force framebuffer backend rejection rather than partial unsafe clearing

### Requirement: Dynamic visible grid drives console viewport behavior

BigOS SHALL use the selected backend's visible grid for runtime console wrapping, cursor placement, viewport redraw, PageUp/PageDown step size, and bottom-follow policy. Scrollback retention MUST remain bounded and console-owned.

#### Scenario: Output wraps at dynamic column count

- **WHEN** ordinary console output reaches the last visible column of a framebuffer dynamic grid
- **THEN** BigOS MUST wrap or scroll according to the console text model using that dynamic column count
- **AND** double-width cells MUST NOT be split across the dynamic line boundary

#### Scenario: Viewport redraw paints dynamic grid

- **WHEN** a viewport redraw occurs while the framebuffer backend is selected
- **THEN** BigOS MUST redraw every visible cell in the dynamic columns-by-rows grid from console-owned state
- **AND** cells outside retained scrollback history MUST render as deterministic blanks

#### Scenario: Page navigation uses dynamic rows

- **WHEN** PageUp or PageDown adjusts the console viewport while a dynamic framebuffer grid is active
- **THEN** BigOS MUST use a bounded page step derived from the current visible row count
- **AND** Home, End, bottom-follow, and history-view new-output behavior MUST remain deterministic

#### Scenario: Scrollback capacity remains bounded

- **WHEN** the framebuffer visible row count is larger than the Legacy VGA row count
- **THEN** BigOS MUST keep scrollback retention within the documented fixed capacity
- **AND** it MUST NOT grow scrollback dynamically, persist history to filesystem, or expose a new terminal ABI
