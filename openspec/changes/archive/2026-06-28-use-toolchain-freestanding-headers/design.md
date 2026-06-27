## Context

BigOS `include/` 下有三份从 GCC vendor 进来的 freestanding C 头副本：`include/stddef.h`、`include/stdint.h`、`include/stdarg.h`（版权头为 GCC、guard 形如 `_GCC_STDINT_H`）。内核构建 `xmake/kernel.lua` 加了 `-I $(projectdir)/include`，因此这些副本会被命中：

- `include/bigos/types.h` `#include <stdint.h>` 与 `<stddef.h>`（提供 `uint*_t`、`size_t`）。
- `include/bigos/io.h`、`kernel/core/bigos/io.cc`、`kernel/core/bigos/panic.cc`、`kernel/mm/slab.cc` `#include <stdarg.h>`（变参日志/panic 路径）。
- `cpp/include/ktl/pair.h` `#include <stddef.h>`。

对比当前工具链（`x86_64-elf 12.2.0`）：工具链 `stdint.h` 仅 14 行，是转发头（freestanding 下 `#include "stdint-gcc.h"`）；仓库副本 362 行其实是把 `stdint-gcc.h` 实体内联。类型最终都来自同一套编译器内建宏（`__INT64_TYPE__` 等），ABI 等价，差异只是「转发 vs 内联实体」与版权快照年份。

约束：

- 保持内核 freestanding-safe，不假设 hosted runtime。
- 不改变目标三元组 `x86_64-elf` 与 LP64 数据模型；类型宽度/对齐由 x86-64 System V psABI 决定。
- 不改启动地址、链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或运行时 ABI。
- 用户态 `user/**` 不在范围内（其构建不加 `-I include`，本就走工具链头）。

## Goals / Non-Goals

**Goals:**

- 删除 `include/{stddef,stdint,stdarg}.h` 三份与工具链重复的 vendor 副本，使内核构建回落到交叉工具链 freestanding 头。
- 保持所有既有 `#include <stddef.h>`/`<stdint.h>`/`<stdarg.h>` 写法不变，仅切换头来源。
- 加入编译期 LP64 x86_64 ABI 断言护栏，把「工具链/环境 ABI 不符」的风险从运行期前移到编译期可观察失败。
- 保证删除后内核 + C++ 支持库构建通过、运行时行为不变。

**Non-Goals:**

- 不清理 `include/` 下其它头（如 `include/string.h`）。
- 不触碰用户态 libc/程序（`user/**`）。
- 不引入 `-nostdinc`、不改 include 搜索策略、不改目标三元组或数据模型。
- 不引入对宿主 libc 的任何依赖。

## Decisions

### 决策 1：直接删除三份副本，依赖工具链 freestanding 头回落

删除 `include/stddef.h`、`include/stdint.h`、`include/stdarg.h`。内核构建用 `-ffreestanding` 且**不带 `-nostdinc`**，GCC 默认把自身 `include/`（含 `stddef.h`/`stdint.h`/`stdbool.h`/`stdarg.h`）放入搜索路径，故删除仓库副本后 `#include <...>` 自然命中工具链版本。已实测内核 C++ 配置（`-mcmodel=kernel -mno-red-zone -mno-sse/-sse2/-mmx -fno-rtti -fno-exceptions -std=c++17`）下三个头均可解析。

- 备选：保留副本但定期与工具链同步。否决：维护负担 + 仍有漂移窗口。
- 备选：改用 `include_next` 包装。否决：无收益，工具链头已是权威来源，包装反而引入多余间接层。

### 决策 2：保持 `#include` 写法与公共头来源边界不变

不改任何源文件的 `#include <stddef.h>`/`<stdint.h>`/`<stdarg.h>`。BigOS 自有公共头（`bigos/`、`drivers/`、`irq/`、`arch/`）与 C++ 支持头（`cpp/include`、`cpp/libsupc++/include`）仍由 `-I include`/`-I cpp/...` 提供，include 边界不变——`-I include` 仍保留，只是其中不再有这三份标准 C 头。

### 决策 3：新增编译期 LP64 x86_64 ABI 断言护栏

在一个内核编译单元普遍可见的位置（倾向 `include/bigos/types.h`，它已 `#include <stdint.h>`/`<stddef.h>` 且被广泛包含）加入：

```cpp
static_assert(sizeof(size_t) == 8, "BigOS assumes LP64 x86_64: size_t is 8 bytes");
static_assert(sizeof(long) == 8, "BigOS assumes LP64 x86_64: long is 8 bytes");
static_assert(sizeof(uint64_t) == 8 && sizeof(uint32_t) == 4, "fixed-width type widths");
static_assert(__CHAR_BIT__ == 8, "BigOS assumes 8-bit byte");
```

这样任何 ABI 不符的工具链/环境在编译期立即报错并指明原因，杜绝静默产出不一致二进制。`__CHAR_BIT__` 用编译器内建宏以避免强行引入 `<limits.h>`；若选用 `CHAR_BIT` 则需 `#include <limits.h>`（工具链经 `include-fixed/` 提供）。

- 备选：放在某个 `.cc` 内部。否决：`types.h` 被广泛包含，断言覆盖面更广且零运行时成本。

### 决策 4：变更范围严格限定为这三份头

只删与工具链重复、且确属标准 C freestanding 头的三份。`include/string.h` 等可能含 BigOS 自有声明或不完全等价，不在本次范围，避免误删导致行为变化。

## Risks / Trade-offs

- [删除后内核找不到头]：若某构建配置实际带了 `-nostdinc` 或裁剪过工具链 → 缓解：删除前后各跑一次 `xmake` 内核构建对比；已实测当前配置可解析；若不可解析则记录并配 `-isystem $(x86_64-elf-gcc -print-file-name=include)`。
- [工具链 ABI 与假设不符]：理论上换到非 LP64 目标 → 缓解：决策 3 的编译期断言当场失败，可观察、不静默。
- [vendor 副本与工具链存在隐藏行为差异]：如某些 `__need_*` 宏路径 → 缓解：副本本就是同源 GCC 头的内联快照，行为等价；以内核构建通过 + 既有 runtime 回归（默认启动 serial marker）作为兜底验证。
- [误删非纯标准头]：→ 缓解：决策 4 限定范围，仅删确证为 GCC vendor 的三份；`include/string.h` 等不动。

## Migration Plan

- 纯删除 + 一处断言增量：先加 ABI 断言（保证护栏先到位），再删三份副本，跑内核构建与默认启动回归。
- 回滚策略：如发现解析或行为问题，恢复三份副本文件即可（git revert 单次提交），不涉及内核逻辑改动。

## Open Questions

- 无未决项。ABI 断言放置点倾向 `include/bigos/types.h`，实现时若发现更合适的单一覆盖点可微调，不影响契约。
