## Context

当前 Legacy BIOS 启动链路已经在 `0x0840` 写入 v1 `BootInfo`，并通过 magic、version、size 和字段 offset 提供基本 ABI 校验。早期内存初始化已经优先尝试读取该结构，但实际仍消费 E820 ARDS 格式，且 kernel entry 仍是无参 `kernel()`：bootloader 跳转到 ELF entry，runtime `_start` 调用 `_init` 后再调用 `kernel()`。

UEFI 启动蓝图要求后续 BIOS 与 UEFI backend 都规范化为统一 handoff，而不是让 kernel 子系统直接消费 BIOS E820、UEFI raw descriptor 或固定低地址别名。因此本 change 应把运行时可落地的 v2 handoff producer/consumer 一次打通：公共 ABI 类型、Legacy BIOS v2 blob 生产、寄存器传递指针、Legacy fallback 和统一 memory region view。

当前控制流：

```text
boot.s -> boot() -> load kernel ELF -> write BootInfo v1 at 0x0840
       -> jump e_entry -> runtime _start -> _init -> kernel()
                                             │
                                             └─ mm reads BootInfo or 0x0500/0x0800 aliases
```

目标控制流：

```text
boot.s -> boot() -> load kernel ELF -> write BootInfo v1 fallback at 0x0840
       -> build complete BootInfo v2 blob with memory map section
       -> rdi = BootInfoHeader* -> jump e_entry -> runtime _start(BootInfo*)
                                          -> preserve/forward pointer
                                          -> kernel(BootInfo*)
                                                   │
                                                   └─ mm consumes BootMemoryRegion view
                                                      backed by v2 memory map section
```

## Goals / Non-Goals

**Goals:**

- 定义 `BootInfoHeader + tagged sections` 的初始 v2 ABI 类型、section metadata、boot protocol 和固定 core 字段，并让 Legacy BIOS backend 生产完整 v2 handoff blob。
- 定义 `BootMemoryRegion`、normalized memory type 和 attributes，使内存模块可以面向统一 memory map consumer 编写。
- 让 Legacy BIOS 路径通过 `rdi` 向 kernel ELF entry 传递 `BootInfo*`，并让 runtime `_start` 转发该指针给 `kernel()`.
- 保留 v1 `BootInfo`、`BIGOS_BOOT_INFO_ADDRESS` 和低地址 E820 aliases 作为故障排查和迁移期 fallback。
- 将 `bigos::init_mem()` 或其下层初始化改造成可接收 boot handoff/memory region view 的路径，同时避免早期动态分配依赖。
- 更新启动布局和 UEFI 蓝图文档，说明本 change 已落地的 ABI 与仍未实现的 UEFI loader 边界。

**Non-Goals:**

- 不实现 `BOOTX64.EFI`、ESP/FAT 镜像生成、QEMU/OVMF 调试入口或 UEFI smoke test。
- 不让 kernel 直接解释 `EFI_MEMORY_DESCRIPTOR`、UEFI system table 或调用 UEFI Boot Services/Runtime Services。
- 不移动现有 fixed low addresses、boot-stage page-table 区域、kernel physical load base 或 higher-half base；v2 blob 的实际存放位置是 loader producer 实现细节，不成为 kernel ABI。
- 不重构现有 BIOS ELF loader 为 UEFI 可复用代码。

## Decisions

### Decision: v1 保持稳定，v2 作为并行 ABI 类型引入

现有 `BootInfo` v1 是可运行 BIOS 路径的一部分，不应通过改字段含义或扩大 struct 的方式破坏。新增 v2 类型应在公共 header 中并行定义，例如 `BootInfoHeader`、`BootInfoSection`、`BootInfoCore`、`BootInfoSectionType` 和相关 ABI 常量。v1 `BootInfo` 的 size、alignment 和 offset static_assert 必须继续保留。

v2 `BootInfo` 使用独立 magic，例如 `BIGOS_BOOT_INFO_V2_MAGIC`，而不是复用 v1 `BIGOS_BOOT_INFO_MAGIC` 后仅依赖 version 区分。v1 magic 只表示 legacy fixed-layout `BootInfo`；v2 magic 表示 header + tagged sections。parser 顺序为先按 v2 magic/size/section 边界解析，失败时再尝试显式 v1 fallback。

Alternatives considered:

- 直接扩展 v1 struct：访问简单，但会让旧 producer 和新 consumer 对同一地址的布局解释不一致。
- v2 复用 v1 magic 并仅通过 version 区分：同一协议族语义清晰，但一旦旧 consumer 校验遗漏，就可能把 v2 header 误读为 v1 fixed struct。
- 只写文档不加类型：风险低但无法为后续实现提供构建期 layout 保护。

### Decision: Legacy BIOS producer 直接生成完整 v2 handoff blob

v2 header 需要包含 magic、version、header size、total size、flags、boot protocol、section count、section table offset 等核心字段。每个 section 至少包含 type、flags、offset、size 和 alignment 或等价边界信息。consumer 必须校验 total size、section table 越界、section payload 越界和 alignment。

本 change 中 Legacy BIOS backend 应在继续写入 v1 `BootInfo` fallback 的同时，直接生产完整 v2 handoff blob。v2 blob 至少包含 boot protocol/core section 和 memory map section；memory map section 由 BIOS E820 ARDS 规范化为 `BootMemoryRegion[]`。`rdi` 指向 v2 `BootInfoHeader`，kernel 通过 v2 blob 初始化；v1 `BootInfo` 和低地址 E820 aliases 只作为 fallback。

v2 section table 和 section payload 使用相对 `BootInfoHeader` 起始地址的 offset/size 描述，长期不绑定固定低地址。Legacy BIOS producer 可以在实现中选择一段不与现有 E820 buffer、legacy aliases、v1 `BootInfo`、boot-stage page table、kernel load base 冲突的 handoff blob 区域，但该物理地址只是 producer 实现细节，不成为 kernel ABI。未来 UEFI loader 同样只需构造等价 blob 并通过寄存器传入。

Alternatives considered:

- 本阶段只定义 v2 契约、不强制 BIOS 生产 sections：风险较低，但不能验证真正的 producer/consumer 边界，后续 UEFI spike 仍会面对未验证的 v2 blob 生命周期问题。
- 将 v2 section table 和 memory map 固定放在低地址：BIOS 实现简单，但会继续扩大 BIOS 魔法地址模型，并迫使未来 UEFI backend 模仿 Legacy 布局。
- 只保留固定 v2 core 字段、不引入 sections：短期实现量低，但无法承载 framebuffer、ACPI/SMBIOS、loader metadata 等变长数据。

### Decision: BootMemoryRegion 使用无分配 iterator/view

早期 buddy 初始化发生在通用 allocator 可用之前，因此统一 memory map consumer 不应要求动态分配 `BootMemoryRegion[]`。本阶段优先实现只读 iterator 或 callback：正常路径直接遍历 v2 memory map section 中的 `BootMemoryRegion[]`；fallback 路径才逐条把 v1 E820 ARDS 转换为 `BootMemoryRegion` 临时值并交给 consumer。

`BootMemoryRegion` 至少包含：

```text
physical_base
length
normalized_type
attributes
source_type
```

normalized type 初始覆盖 usable、reserved、acpi_reclaim、acpi_nvs、mmio、loader、kernel、bad_memory、runtime。attributes 至少预留 runtime、write-back、write-combine、uncacheable、firmware-specific bits 的表达空间。

Alternatives considered:

- 在 bootloader 中生成完整数组：对未来 UEFI 合适，但当前 BIOS 迁移需要先找到安全存放位置，且会增加低地址布局压力。
- 让 buddy 同时支持 E820 和 UEFI descriptor：短期方便，但违背统一 handoff 原则。

### Decision: register ABI 使用 rdi，并在 runtime _start 中显式保存

x86_64 第一个参数寄存器使用 `rdi`。BIOS `boot.s` 在跳转 ELF entry 前设置 `rdi = v2 BootInfoHeader*`。由于实际 ELF entry 是 runtime `_start`，而 `_start` 会先调用 `_init`，所以 `_start` 必须保存入口 `BootInfo*`，调用 `_init` 后再恢复为 `kernel()` 的第一个参数。

kernel entry 改为接收 `const BootInfo*` 或等价公共 handoff 指针。若传入指针为空、magic/version/size 校验失败，kernel 允许显式 fallback 到 `BIGOS_BOOT_INFO_ADDRESS`；fallback 失败时内存初始化必须早期失败或停止继续误用未知数据。

Alternatives considered:

- 继续只从固定低地址读取：兼容当前 BIOS，但未来 UEFI backend 会被迫仿造 BIOS 地址布局。
- 直接把 v2 指针传给 kernel 并移除 v1 fallback：ABI 更干净，但会破坏现有 BIOS 迁移路径。

### Decision: failure behavior 保守而显式

Boot handoff parser 遇到未知 magic、unsupported version、过小 size、section 越界或 alignment 错误时，不得静默解释未知数据。早期内存初始化如果没有可用的 memory map，必须停止初始化或进入明确的 panic/halt 路径；不得假设全物理内存可用。

对于 memory type，只有 `usable` 区域能交给 buddy。reserved、runtime、mmio、acpi_reclaim、acpi_nvs、bad_memory 和未知类型必须保守排除。`acpi_reclaim` 在尚未实现 ACPI 表发现、复制和生命周期管理前必须完全保留；未来只有在 ACPI 初始化明确证明相关表不再被引用后，才能通过单独回收阶段释放。

## Risks / Trade-offs

- [Risk] ABI 迁移破坏现有 BIOS boot。-> Mitigation：v1 layout 不变，`BIGOS_BOOT_INFO_ADDRESS` 不变，新增寄存器传参同时保留低地址 fallback，并用 `make boot-debug` 验证。
- [Risk] `_start` 调用 `_init` 破坏 `rdi`，导致 kernel 收到错误指针。-> Mitigation：runtime 汇编显式保存入口参数，再恢复传给 `kernel()`.
- [Risk] BIOS producer 直接生成完整 v2 blob 增加低层启动阶段复杂度。-> Mitigation：v1 fallback 保持不变，v2 blob 地址作为 producer 实现细节单独文档化，并通过 strict bounds/layout 校验和 `make boot-debug` 验证。
- [Risk] memory map consumer 过早抽象导致 allocator 初始化顺序复杂。-> Mitigation：采用无分配 iterator/callback，不在 buddy 初始化前要求堆分配。
- [Risk] UEFI attributes 设计不足导致未来返工。-> Mitigation：attributes 使用 64-bit bitmask，并保留 source_type/firmware-specific 信息。
- [Risk] clang/clangd 对 freestanding cross-build 产生误报。-> Mitigation：将 clang/clangd 作为辅助检查，最终以 xmake/cross GCC build 和 boot smoke test 为准。

## Migration Plan

1. 增加公共 ABI 类型和 static_assert，不改变 v1 `BootInfo` 行为。
2. 在 Legacy BIOS backend 中继续写入 v1 `BootInfo`，同时构造完整 v2 handoff blob，包括 boot protocol/core section 和 memory map section。
3. 增加 boot handoff parser，优先解析 register-passed v2 pointer，失败时显式 fallback 到 v1 fixed low-address `BootInfo`。
4. 修改 `boot.s` 设置 `rdi` 为 v2 `BootInfoHeader*`，修改 `crt0.s` 保存并转发 `BootInfo*`，修改 `kernel()` 签名。
5. 引入 `BootMemoryRegion` view，把 buddy 初始化从直接 ARDS 遍历迁移到统一 consumer。
6. 更新文档和 OpenSpec 验证记录，确认 UEFI loader 仍为后续 change。

Rollback strategy:

- 如果 v2 blob 生产或寄存器传参导致启动失败，可保留 `kernel()` 内部 fixed-address fallback，并临时回滚 v2 producer 或 `crt0.s`/`kernel()` 签名相关改动。
- 如果统一 memory region consumer 出现问题，可回退 buddy 的 ARDS 直接遍历路径；v1 `BootInfo` 和低地址 aliases 未被移除。

## Resolved Decisions

- v2 `BootInfo` 使用新的 magic；v1 magic 只表示 legacy fixed-layout `BootInfo`，parser 按 v2 -> v1 fallback 顺序解析。
- v2 section table 和 section payload 使用相对 `BootInfoHeader` 的 offset/size 描述，长期不绑定固定低地址；本 change 要求 Legacy BIOS backend 直接生成完整 v2 handoff blob，并通过寄存器传入其 header 指针。
- `acpi_reclaim` 在 ACPI 子系统完成表生命周期管理前保守保留；本 change 的 buddy 初始化只释放 usable region。
