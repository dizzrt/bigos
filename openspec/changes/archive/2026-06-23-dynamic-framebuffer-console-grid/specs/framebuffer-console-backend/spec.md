## ADDED Requirements

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
