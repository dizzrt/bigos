## Why

当前 KTL 实现较小，并已被 buddy、slab、vmem 和早期内核工具使用，但部分组件在位图计数、边界检查、对象生命周期和缓冲区状态维护上存在正确性风险。`temp/bigos_` 中保留了早期 KTL 实验实现，包括 allocator、list、tree、map、queue、priority queue 等雏形，应作为参考材料进行筛选、重写和整合，而不是直接照搬到当前活动 KTL 中。

## What Changes

- 加固现有 KTL 组件：`ktl::bitset`、`ktl::buffer`、`ktl::intrusive_list`、`ktl::intrusive_list_node` 和 `ktl::pair`。
- **BREAKING**：统一 KTL 公开组件命名，直接将 `ktl::Buffer` 改为 `ktl::buffer`，将 `ktl::klist`/`ktl::klist_node` 改为 `ktl::intrusive_list`/`ktl::intrusive_list_node`，不提供旧名 alias。
- 修复 bitmap 扫描、`set/reset` 计数、边界处理、失败处理，以及 buffer 读写状态更新中的已知正确性问题。
- 必要时重构 KTL API，使所有权、对象生命周期、侵入式节点语义和失败行为显式化。
- 参考 `temp/bigos_` 中对当前内核有价值的能力，补齐 allocator traits/helpers、algorithm utilities、queue/priority_queue adapters、ordered-tree/map foundations 等能力。
- 保持所有 KTL 工作满足 freestanding kernel 约束：不使用异常、不使用 RTTI、不依赖 hosted runtime，不引入超出仓库 freestanding C++ 支持范围的隐式 libc/STL 依赖。
- 为 KTL 行为及依赖 KTL 的现有内存管理用户补充聚焦验证。
- 在 `/Users/bytedance/Desktop/workspace/kernel/bigos/docs/ktl` 下补充组件级文档，覆盖能力、使用方式、所有权规则和限制。

## Capabilities

### New Capabilities
- `ktl-components`: 活动 KTL 组件能力，包括现有容器/工具的加固，以及新增的 freestanding-safe KTL 能力。
- `ktl-documentation`: 面向开发者的 KTL 文档能力，包括每个组件的用法示例和内核开发约束说明。

### Modified Capabilities
- 无。

## Impact

- 影响代码：`cpp/include/ktl`、`cpp/ktl`、必要的 freestanding 支撑头文件，以及 `mm`、`kernel` 和未来 KTL 使用方中的调用点。
- 影响文档：`docs/ktl`。
- 影响验证：KTL 聚焦检查、buddy/slab/vmem 集成审查、可用时的 xmake 交叉构建，以及可行时的 clang/clangd 辅助诊断。
- 兼容风险：本 change 会一步到位迁移 `mm`、`kernel` 和文档中的旧 KTL 名称；必须保持内存管理子系统依赖的链表和位图行为，但不保留旧名兼容层。
