## MODIFIED Requirements

### Requirement: Public headers remain separate from implementation sources

BigOS SHALL keep public kernel headers and C++ support library headers in documented include roots so that include semantics are not tied to concrete implementation paths. Freestanding standard C headers that duplicate the cross toolchain (`stddef.h`、`stdint.h`、`stdarg.h`) MUST NOT be vendored under `include/`; they MUST be sourced from the cross toolchain freestanding header set, while BigOS-owned public headers (such as `bigos/`、`drivers/`、`irq/`、`arch/`) and C++ support headers remain in their documented include roots.

#### Scenario: Existing public include style remains valid

- **WHEN** source files include public headers such as `<bigos/io.h>`, `<bigos/memory.h>`, `<irq/interrupt.h>`, `<arch/x86/boot/boot_info.h>`, `<ktl/list.h>`, `<drivers/video/vga.h>`, or `<drivers/irqchip/i8259.h>`
- **THEN** the configured include search paths resolve those headers without requiring source files to include `kernel/` in public include directives
- **AND** KTL, `bits`, `ext`, and libsupc++ public or semi-public C++ support headers are resolved from documented `cpp/` include roots

#### Scenario: Public API boundary is reviewed

- **WHEN** implementation files are moved under `kernel/`
- **THEN** headers that remain public are available through documented include roots
- **AND** C++ support headers remain under `cpp/include` or `cpp/libsupc++/include` instead of being folded into the top-level kernel `include/`
- **AND** private implementation headers are either kept with their subsystem or explicitly documented as implementation-only include roots

#### Scenario: 重复的标准 C freestanding 头不由仓库提供

- **WHEN** the kernel or C++ support build resolves `<stddef.h>`、`<stdint.h>` 或 `<stdarg.h>`
- **THEN** these headers MUST be provided by the cross toolchain freestanding header set rather than vendored copies under `include/`
- **AND** removing the vendored copies MUST NOT change any `#include` directive form or the BigOS-owned public header include roots
