## Purpose

Define the documentation requirements for BigOS KTL public components, including component references, examples, migration notes, and unsupported capability status.

## Requirements

### Requirement: 组件文档
KTL SHALL 在 `docs/ktl` 下为每个公共组件提供文档。

#### Scenario: 现有组件文档
- **WHEN** 开发者查阅 `bitset`、`buffer`、`intrusive_list`、`intrusive_list_node` 或 `pair`
- **THEN** `docs/ktl` MUST 包含说明该组件用途、头文件路径、命名空间、构造方式、操作、所有权规则、失败行为和限制的文档

#### Scenario: 旧名不作为活动 API 出现
- **WHEN** 文档描述活动 KTL API
- **THEN** 文档 MUST NOT 将 `ktl::Buffer`、`ktl::klist` 或 `ktl::klist_node` 描述为可用 API 或提供旧名使用示例

#### Scenario: 新组件文档
- **WHEN** 本 change 新增 allocator、algorithm、queue、priority queue、tree、map 或其他公共 KTL 组件
- **THEN** `docs/ktl` MUST 在对应任务完成前包含新组件文档

### Requirement: 使用示例
KTL 文档 SHALL 包含适用于 freestanding kernel code 的小型使用示例。

#### Scenario: 示例约束
- **WHEN** 文档展示示例代码
- **THEN** 示例 MUST 避免 hosted runtime 假设、异常、RTTI、线程、文件、socket 和非内核依赖

#### Scenario: 内存管理示例
- **WHEN** 文档覆盖 buddy、slab、kmalloc 或 virtual memory 使用的组件
- **THEN** 示例或说明 MUST 描述相关内存所有权、分配阶段和生命周期约束

### Requirement: 迁移说明
KTL 文档 SHALL 记录现有 KTL 用户需要关注的行为变化和迁移说明。

#### Scenario: 行为变化
- **WHEN** KTL API 的语义、返回值、失败行为、所有权行为或失效规则发生变化
- **THEN** `docs/ktl` MUST 记录旧风险、新行为和所需调用点迁移；旧名只能出现在迁移说明中，不得作为活动 API 出现

#### Scenario: Temp 参考映射
- **WHEN** 新活动 KTL 组件受到 `temp/bigos_` 启发
- **THEN** 文档 MUST 说明活动组件行为，并避免将 temp 实现呈现为受支持 API

### Requirement: 文档索引
KTL 文档 SHALL 提供帮助开发者选择正确组件的索引。

#### Scenario: 组件选择
- **WHEN** 开发者打开 KTL 文档目录
- **THEN** MUST 存在索引或概览，列出所有已文档化组件并总结每个组件适用场景

#### Scenario: 不支持能力
- **WHEN** 某项能力仍被有意保持为不支持或实验状态
- **THEN** 文档 MUST 说明该状态以及它不是活动 KTL API 的原因
