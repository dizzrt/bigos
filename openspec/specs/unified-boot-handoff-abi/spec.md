## Purpose

Define the unified boot handoff ABI used by x86 boot backends to pass early boot metadata to the BigOS kernel. The initial ABI uses a versioned `BootInfoHeader + tagged sections` blob, keeps the existing v1 `BootInfo` layout as a Legacy BIOS migration fallback, defines register-passed handoff semantics, and exposes firmware memory maps through a normalized `BootMemoryRegion` view.

## Requirements

### Requirement: BootInfo v2 handoff header

BigOS SHALL define and produce a versioned boot handoff ABI based on `BootInfoHeader + tagged sections` while preserving the existing v1 `BootInfo` layout for Legacy BIOS fallback.

#### Scenario: 公共 header 定义 v2 ABI

- **WHEN** 构建 boot C++ 或 kernel C++ 代码
- **THEN** 公共 boot handoff header MUST 定义 `BootInfoHeader`、`BootInfoSection`、boot protocol、section type、section flags 和少量固定 core 字段
- **AND** 它 MUST 提供 size、alignment 和关键字段 offset 的构建期校验

#### Scenario: v2 使用独立 magic

- **WHEN** v2 handoff ABI 被定义
- **THEN** v2 `BootInfoHeader` MUST 使用不同于 v1 `BIGOS_BOOT_INFO_MAGIC` 的 magic
- **AND** kernel parser MUST NOT 仅依赖 version 区分 v1 fixed-layout `BootInfo` 和 v2 header/sections blob

#### Scenario: v1 layout 保持兼容

- **WHEN** 新 v2 ABI 类型被引入
- **THEN** 现有 v1 `BootInfo` 的 magic、version、size、alignment、field offset 和 `BIGOS_BOOT_INFO_ADDRESS` MUST 保持兼容
- **AND** Legacy BIOS fallback consumer MUST 能继续校验并读取 v1 `BootInfo`

#### Scenario: tagged section 边界可校验

- **WHEN** kernel consumer 解析 v2 handoff data
- **THEN** 它 MUST 校验 header size、total size、section table offset、section count、section payload offset、section payload size 和 alignment 不越界
- **AND** 未识别的非必需 section MUST 被跳过，必需 section 缺失或格式错误 MUST 导致早期失败或显式 fallback

#### Scenario: Legacy BIOS 生产完整 v2 blob

- **WHEN** Legacy BIOS bootloader 完成 kernel ELF64 加载并收集启动元数据
- **THEN** 它 MUST 生成完整 v2 handoff blob，至少包含 boot protocol/core section 和 memory map section
- **AND** section table 和 section payload MUST 使用相对 `BootInfoHeader` 起始地址的 offset/size 描述，而不是要求 kernel 依赖固定低地址

### Requirement: Register-passed BootInfo pointer

BigOS SHALL use a register-passed `BootInfo*` as the primary kernel entry handoff path on x86_64, with fixed low-address lookup retained only as a migration fallback.

#### Scenario: bootloader 跳转 ELF entry 前设置参数

- **WHEN** Legacy BIOS bootloader 完成 ELF64 kernel 加载并准备跳转到已校验 entry point
- **THEN** 它 MUST 在 x86_64 第一个参数寄存器中传递 `BootInfo*`
- **AND** 该指针 MUST 指向当前已生成并可校验的 v2 `BootInfoHeader`

#### Scenario: runtime startup 转发参数

- **WHEN** kernel ELF entry 进入 runtime `_start`
- **THEN** `_start` MUST 保存入口 `BootInfo*`，执行必要 runtime 初始化后将同一指针作为第一个参数传给 `kernel()`
- **AND** `_start` MUST NOT 依赖 `_init` 保持参数寄存器不变

#### Scenario: kernel entry fallback 明确

- **WHEN** `kernel()` 收到空指针或校验失败的 handoff 指针
- **THEN** kernel MAY 尝试读取文档化 fixed low-address fallback
- **AND** 如果 fallback 也不可用，kernel MUST 早期失败或停止继续依赖未知启动数据

### Requirement: Unified BootMemoryRegion view

BigOS SHALL expose early physical memory information through a unified `BootMemoryRegion` view instead of requiring memory initialization to directly consume firmware-specific descriptors.

#### Scenario: BootMemoryRegion 格式可表达统一内存图

- **WHEN** 定义统一 memory map consumer ABI
- **THEN** `BootMemoryRegion` MUST 至少表达 physical base、length、normalized type、attributes 和 source type
- **AND** normalized type MUST 至少覆盖 usable、reserved、acpi_reclaim、acpi_nvs、mmio、loader、kernel、bad_memory 和 runtime

#### Scenario: BIOS E820 写入 v2 memory map section

- **WHEN** Legacy BIOS backend 收集 BIOS E820 entries
- **THEN** 它 MUST 将 E820 entries 规范化为 `BootMemoryRegion` entries 并写入 v2 memory map section
- **AND** E820 type 1 MUST 映射为 usable，type 2 MUST 映射为 reserved，type 3 MUST 映射为 acpi_reclaim，type 4 MUST 映射为 acpi_nvs，type 5 MUST 映射为 bad_memory，未知类型 MUST 保守映射为 reserved

#### Scenario: 早期内存初始化只释放 usable 区域

- **WHEN** buddy allocator 初始化消费 `BootMemoryRegion` view
- **THEN** 它 MUST 只把 normalized type 为 usable 的区域加入 free list
- **AND** reserved、runtime、mmio、acpi_reclaim、acpi_nvs、bad_memory 和未知区域 MUST 被保守排除

#### Scenario: v1 E820 fallback 显式受限

- **WHEN** v2 handoff blob 缺失或校验失败但 v1 `BootInfo` fallback 可用
- **THEN** kernel MAY 将 v1 E820 entries 临时规范化为 `BootMemoryRegion` view
- **AND** fallback 路径 MUST 被显式记录为迁移期兼容路径，不得成为长期 primary ABI

#### Scenario: memory map view 不依赖早期堆分配

- **WHEN** 早期 buddy allocator 尚未完成初始化
- **THEN** `BootMemoryRegion` view MUST 支持无动态分配的遍历方式，例如 callback、iterator 或栈上临时转换
- **AND** 它 MUST NOT 要求 slab、kmalloc 或通用 heap 已经可用

### Requirement: Handoff validation and documentation

BigOS SHALL validate and document boot handoff ABI assumptions whenever this change touches entry registers, fixed addresses, or early memory consumers.

#### Scenario: ABI 和地址假设被文档化

- **WHEN** unified handoff ABI 被实现
- **THEN** 启动架构文档 MUST 记录 register-passed v2 `BootInfoHeader*`、Legacy BIOS v2 blob 生产、v1 fixed-address fallback、v2 header/section 关系和 `BootMemoryRegion` consumer 迁移状态
- **AND** 文档 MUST 明确 UEFI loader、ESP image 和 OVMF/QEMU 调试入口仍不属于本 change 的已实现能力

#### Scenario: 构建与启动验证覆盖 handoff

- **WHEN** 本 change 的运行时代码修改完成
- **THEN** 验证记录 MUST 覆盖 cross-toolchain build、Legacy BIOS boot smoke test 或无法运行时的明确原因
- **AND** 验证记录 MUST 包含 ABI/layout compatibility review、early memory initialization review 和辅助 clang/clangd 诊断结论
