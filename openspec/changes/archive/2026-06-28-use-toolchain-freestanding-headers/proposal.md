## Why

仓库在 `include/` 下 vendor 了三份来自 GCC 的 freestanding C 头副本 `stddef.h`/`stdint.h`/`stdarg.h`，内核构建经 `-I include` 命中这些副本（被 `include/bigos/types.h`、`include/bigos/io.h`、`cpp/include/ktl/pair.h` 与若干 `kernel/**` 源文件 `#include` 间接消费）。这些副本是版本冻结的快照（版权年份停在 `2008-2022`），不会随交叉工具链升级而更新。一旦切换工具链版本或编译环境，工具链的代码生成与内建宏会前进，而冻结副本不动，存在与工具链内建宏/类型假设发生静默漂移的风险。

x86_64-elf 工具链在 `-ffreestanding` 下本就必须提供这几个标准头（freestanding C 强制要求），其类型/宏由 x86-64 System V psABI + LP64 数据模型决定。直接复用工具链版本既消除冻结快照漂移风险，又减少维护负担。

## What Changes

- 删除 `include/stddef.h`、`include/stdint.h`、`include/stdarg.h` 三份 vendor 自 GCC 的 freestanding 头副本，使内核构建在 `-ffreestanding` 下回落到交叉工具链自带的同名标准头。
- 内核源码与公共头中对 `<stddef.h>`/`<stdint.h>`/`<stdarg.h>` 的既有 `#include`（如 `include/bigos/types.h`、`include/bigos/io.h`、`cpp/include/ktl/pair.h`、`kernel/core/bigos/io.cc`、`kernel/core/bigos/panic.cc`、`kernel/mm/slab.cc`）保持不变；仅头来源由仓库副本切换为工具链版本。
- 新增编译期 ABI 断言护栏：在一个内核可见的位置加入 `static_assert`/`_Static_assert`，固定 LP64 x86_64 假设（如 `sizeof(size_t)==8`、`sizeof(long)==8`、`CHAR_BIT==8`、`sizeof(uint64_t)==8`），使任何 ABI 不符的工具链/环境在编译期立即失败而非静默产出不一致二进制。
- 不改动用户态：`user/**` 已经走工具链 freestanding 头（用户构建不加 `-I include`），本变更不触碰 `user/libc/**`。
- 不改动 `include/string.h` 等其它 `include/` 下的头；本变更范围严格限定为这三份与工具链重复的标准 C freestanding 头。

## Capabilities

### New Capabilities

- `kernel-freestanding-header-sourcing`: 定义内核 freestanding 标准 C 头（`stddef.h`/`stdint.h`/`stdarg.h`）由交叉工具链提供而非仓库 vendor 副本的来源契约、LP64 x86_64 ABI 断言护栏，以及由此保证的 ABI 一致性与可观察构建失败边界。

### Modified Capabilities

- `source-layout-organization`: 既有「公共头与实现源分离」要求把 freestanding C 头子集列为驻留在 `include/` 文档化 include root 的内容；修改为这些与工具链重复的标准 C freestanding 头不再由仓库 `include/` 提供副本，而是来自交叉工具链 freestanding 头，同时保持 BigOS 自有公共头（`bigos/`、`drivers/`、`irq/` 等）与 C++ 支持头的 include 边界不变。

## Impact

- 影响 `include/stddef.h`、`include/stdint.h`、`include/stdarg.h`（删除）；影响内核构建的头解析来源（由 `-I include` 命中副本改为工具链 freestanding 头），不改变任何 `#include` 指令写法。
- 影响范围限于内核与 C++ 支持库构建路径（`kernel/**`、`cpp/**`、`include/bigos/**`）；不影响用户态 libc/程序构建（`user/**` 本就不依赖这三份副本）。
- 已实测确认（`x86_64-elf-g++ 12.2.0`，`-ffreestanding -mcmodel=kernel -mno-red-zone -mno-sse/-sse2/-mmx -fno-rtti -fno-exceptions`，不带 `-nostdinc`）：删除副本后内核 C++ 仍能解析 `<stdint.h>`/`<stddef.h>`/`<stdarg.h>` 且 LP64 断言通过。
- 不改变启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或任何运行时 ABI；类型宽度与对齐由同一 LP64 psABI 决定，删除冗余副本不改变生成代码。
- 非目标：不清理 `include/` 下其它头、不改用户态 libc 头、不引入 `-nostdinc`、不改变目标三元组或数据模型、不新增对宿主 libc 的依赖。
- 架构与工具链假设：当前 x86_64 freestanding 内核，xmake + x86_64-elf GCC/binutils 交叉工具链；辅助 Python 验证（若涉及）通过 `uv run ...`。
