## ADDED Requirements

### Requirement: 统一 KTL 公开命名
KTL SHALL 使用统一的活动公开组件命名：`ktl::bitset`、`ktl::buffer`、`ktl::intrusive_list`、`ktl::intrusive_list_node` 和 `ktl::pair`。

#### Scenario: 不保留旧名 alias
- **WHEN** 本 change 完成 KTL 命名迁移
- **THEN** 代码中 MUST NOT 为 `ktl::Buffer`、`ktl::klist` 或 `ktl::klist_node` 提供 alias、typedef 或兼容 wrapper

#### Scenario: 调用点直接迁移
- **WHEN** `mm`、`kernel` 或其他活动代码使用 KTL buffer 或侵入式链表能力
- **THEN** 调用点 MUST 直接使用 `ktl::buffer`、`ktl::intrusive_list` 和 `ktl::intrusive_list_node`

#### Scenario: 旧名清理
- **WHEN** 命名迁移完成
- **THEN** 活动代码和活动 API 文档 MUST 删除或更新旧名 `ktl::Buffer`、`ktl::klist` 和 `ktl::klist_node`

### Requirement: 加固位图组件
KTL 位图组件 SHALL 为内核 allocator 提供确定性的 bit 占用状态跟踪能力。

#### Scenario: 保持计数一致的 set 和 reset
- **WHEN** 调用方设置或重置单 bit、区间、字节对齐区间或整个位图
- **THEN** 组件 MUST 只按实际发生变化的 bit 更新已用和空闲计数

#### Scenario: 安全处理 scan 失败
- **WHEN** 调用方扫描不存在的连续空闲区间
- **THEN** 组件 MUST 返回文档化的失败值，且调用方 MUST 能在将结果作为索引使用前检测失败

#### Scenario: 边界安全访问
- **WHEN** 调用方请求位图大小范围内或范围外的 bit/区间
- **THEN** 组件 MUST 避免越界读写，并 MUST 以安全 no-op 或文档化失败方式响应非法访问

#### Scenario: 明确定义初始化状态
- **WHEN** 位图基于调用方提供的存储或内部申请的存储构造
- **THEN** 组件 MUST 文档化并执行该存储的初始状态约定，包括清零、填充或调用方已初始化

### Requirement: 环形缓冲区状态安全
KTL 环形缓冲区组件 SHALL 在空、满、部分读取和部分写入场景下保持内部状态一致。

#### Scenario: 空缓冲区批量读取
- **WHEN** 调用方从空 buffer 执行批量读取
- **THEN** 组件 MUST 返回读取 0 字节，并且 MUST NOT 让 size 下溢、移动 head 或破坏 tail

#### Scenario: 容量和分配失败
- **WHEN** buffer 以零容量构造或内部存储分配失败
- **THEN** 组件 MUST 进入文档化的 invalid 或 empty 状态，避免除零和非法内存访问

#### Scenario: 借用存储与自有存储
- **WHEN** buffer 使用外部存储或内部存储构造
- **THEN** 组件 MUST 明确所有权和清理行为

### Requirement: 侵入式链表基础设施
KTL 链表基础设施 SHALL 为调用方持有节点的双向侵入式链表提供安全操作。

#### Scenario: 节点生命周期所有权
- **WHEN** 调用方插入、擦除或移除节点
- **THEN** 组件 MUST 仅 unlink 节点，不隐式释放调用方持有的内存，并 MUST 文档化节点生命周期要求

#### Scenario: 拷贝安全
- **WHEN** 代码尝试拷贝 list foundation 或 node 类型，且拷贝会不安全地复制链接状态
- **THEN** 组件 MUST 阻止不安全拷贝，或显式定义安全的移动/拷贝语义

#### Scenario: 对象构造
- **WHEN** list node 存储非平凡值类型
- **THEN** 组件 MUST 按 C++ 对象生命周期规则构造和销毁该值，或在编译期拒绝不支持的值类型

### Requirement: Pair 和算法工具
KTL utility 层 SHALL 提供 KTL 容器需要的 freestanding-safe 值工具和算法辅助。

#### Scenario: Pair 工具
- **WHEN** 代码使用 `ktl::pair` 进行 key/value 或 tuple-like 值传递
- **THEN** 组件 MUST 提供文档化的构造、比较和 `make_pair` 行为，且不要求 hosted STL

#### Scenario: 算法工具
- **WHEN** 容器需要 swap、min/max、move/forward wrapper 或 heap 操作等基础辅助能力
- **THEN** KTL SHALL 提供或复用 freestanding-safe helper，且不引入异常、RTTI 或 hosted runtime 假设

### Requirement: Allocator Foundation
KTL SHALL 为 owning containers 提供最小 allocator foundation。

#### Scenario: 显式分配策略
- **WHEN** owning KTL container 分配或释放节点
- **THEN** 它 MUST 使用文档化 allocator 接口或策略，而不是隐藏的 hosted allocation

#### Scenario: 分配失败
- **WHEN** allocator-backed KTL 组件发生分配失败
- **THEN** 组件 MUST 通过文档化的无异常机制暴露失败

### Requirement: Queue 和 Priority Queue 适配器
KTL SHALL 在后端存储和失败行为被定义后提供 queue 风格适配器。

#### Scenario: FIFO queue 操作
- **WHEN** 调用方通过 queue adapter push 和 pop 值
- **THEN** 组件 MUST 保持 FIFO 顺序，并文档化其存储是侵入式、固定容量还是 allocator-backed

#### Scenario: Priority queue 顺序
- **WHEN** 调用方向 priority queue adapter 中 push 值
- **THEN** 组件 MUST 按 comparator 返回值，并文档化容量、分配和 comparator 约束

### Requirement: 有序树和 Map 基础设施
KTL SHALL 仅在树不变量和分配行为可测试、可文档化时提供 ordered tree foundation 与 map abstraction。

#### Scenario: 有序迭代
- **WHEN** 调用方遍历 ordered tree 或 map
- **THEN** 组件 MUST 按 comparator 定义的 key 顺序访问条目

#### Scenario: 插入和删除不变量
- **WHEN** 调用方插入或删除条目
- **THEN** 组件 MUST 保持红黑树不变量，或在不破坏树结构的情况下安全失败

#### Scenario: Map 查找
- **WHEN** 调用方查找存在或不存在的 key
- **THEN** map 组件 MUST 以无异常方式返回文档化的命中或未命中结果

### Requirement: 现有内存管理兼容性
KTL 变更 SHALL 保持当前 buddy、slab、kmalloc 和 virtual-memory 用户需要的行为。

#### Scenario: Slab 位图分配
- **WHEN** slab allocation 使用 KTL bitmap scan 和 set 操作
- **THEN** allocation MUST 不使用越界对象索引，且 free-object 计数 MUST 保持一致

#### Scenario: 侵入式内存链表
- **WHEN** buddy、slab 或 virtual-memory 代码插入、擦除或合并 KTL list node
- **THEN** list 链接 MUST 保持有效，且节点内存所有权 MUST 仍归内存管理子系统
