## Purpose

定义 BigOS 内核与 C++ 支持构建中 freestanding 标准 C 头（`stddef.h`、`stdint.h`、`stdarg.h`）的来源契约：这些与交叉工具链重复的标准头由交叉工具链 freestanding 头集合提供，而非仓库在 `include/` 下 vendor 的冻结副本，从而消除随工具链升级产生的静默漂移风险。该能力同时固定 LP64 x86_64 ABI 的编译期护栏，使任何数据模型不符的工具链/环境在编译期确定性失败，而非静默产出不一致二进制。

## Requirements

### Requirement: 内核 freestanding 标准头来自交叉工具链

BigOS SHALL source the freestanding standard C headers `stddef.h`、`stdint.h` 和 `stdarg.h` for kernel and C++ support builds from the cross toolchain's freestanding header set rather than repository-vendored copies under `include/`. The repository MUST NOT keep vendored GCC copies of these three headers under `include/`. Kernel and C++ support sources MAY continue to `#include <stddef.h>`、`<stdint.h>` 或 `<stdarg.h>` unchanged, and the build MUST resolve them through the cross toolchain freestanding headers under `-ffreestanding` without `-nostdinc`.

#### Scenario: 内核源码包含标准头由工具链解析

- **WHEN** a kernel or C++ support source includes `<stddef.h>`、`<stdint.h>` 或 `<stdarg.h>`
- **THEN** the build MUST resolve the header from the cross toolchain freestanding header set
- **AND** the repository MUST NOT provide vendored copies of these three headers under `include/`

#### Scenario: include 写法保持不变

- **WHEN** existing kernel headers and sources such as `include/bigos/types.h`、`include/bigos/io.h` 或 `cpp/include/ktl/pair.h` reference these standard headers
- **THEN** their `#include` directives MUST remain unchanged in form
- **AND** only the resolved header source MUST shift from the repository copy to the toolchain version

### Requirement: LP64 x86_64 ABI 编译期护栏

BigOS SHALL enforce the LP64 x86_64 data-model assumptions at compile time so that an incompatible toolchain or build environment fails deterministically instead of silently producing inconsistent binaries. A widely included kernel header SHALL contain compile-time assertions covering at least that `size_t` 与 `long` are 8 bytes, fixed-width types (`uint64_t`/`uint32_t`) have their nominal widths, and the byte is 8 bits.

#### Scenario: ABI 不符时编译期失败

- **WHEN** the kernel is compiled with a toolchain whose data model violates the LP64 x86_64 assumptions
- **THEN** the compile-time assertions MUST fail the build with a descriptive message
- **AND** the failure MUST occur at compile time rather than as a silent runtime inconsistency

#### Scenario: 符合 LP64 x86_64 时构建通过

- **WHEN** the kernel is compiled with the supported `x86_64-elf` LP64 toolchain
- **THEN** the ABI assertions MUST pass
- **AND** they MUST NOT introduce any runtime cost
