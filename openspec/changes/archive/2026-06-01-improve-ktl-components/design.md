## Context

活动 KTL 位于 `cpp/include/ktl` 和 `cpp/ktl`，已经被编译进内核二进制，并被内存管理代码使用：

- `ktl::bitset` 用于 slab 对象分配状态位图。
- `ktl::intrusive_list` 和 `ktl::intrusive_list_node` 用于组织 buddy、slab、cache 和虚拟内存链表。
- `ktl::buffer` 提供简单环形缓冲区。
- `ktl::pair` 提供 freestanding 环境下的小型 pair-like 值传递工具。

非活动的 `temp/bigos_` 目录保留了早期 KTL 实验代码，包括 `kbitset`、linked containers、allocator 雏形、list 变体、红黑树、map、queue 和 priority queue。这些文件可作为设计输入，但未接入构建，且存在命名空间混杂、TODO 和正确性问题。本 change 将其视为参考材料，而不是可直接复制的实现。

## Goals / Non-Goals

**Goals:**

- 保持 freestanding kernel 约束：C++17、无异常、无 RTTI、无 hosted runtime 假设、无 OS 服务依赖。
- 在扩展 API 前，优先修复活动 KTL 组件的正确性问题。
- 保持 KTL 组件小型、显式，并适合早期内核环境。
- 统一活动 KTL 组件命名为小写 snake_case/语义化类型名：`ktl::bitset`、`ktl::buffer`、`ktl::intrusive_list`、`ktl::intrusive_list_node`、`ktl::pair`。
- 明确所有权和生命周期规则，尤其是侵入式节点与 buffer 存储。
- 在当前或近期内核需求明确时，补齐缺失 KTL 能力。
- 在 `docs/en/ktl` 下为每个 KTL 组件提供文档。
- 通过聚焦检查和受影响内存管理调用点审查验证 KTL 变更。

**Non-Goals:**

- 不实现完整 ISO C++ STL 替代品。
- 不引入异常、RTTI、重度堆抽象、线程、文件、socket 或 hosted 库依赖。
- 不未经审查直接导入全部 `temp/bigos_` KTL 代码。
- 不在本 change 中修改启动地址、链接假设、中断 ABI 或内存布局。
- 不强制要求本可保持侵入式或调用方持有的组件改为动态分配。
- 不提供 `ktl::Buffer`、`ktl::klist` 或 `ktl::klist_node` 的旧名 alias 兼容层。

## Decisions

### Decision: 先稳定现有组件

先修复 `bitset`、`buffer`、`intrusive_list` 和 `pair`，再新增组件。

理由：这些组件已经影响 slab、buddy 和 vmem 行为。在不稳定基础上扩展 KTL 会放大内核风险。

备选方案：

- 先添加缺失容器：拒绝，因为 `bitset` 和 `buffer` 存在即时正确性风险。
- 用 `temp/bigos_` 替换当前 KTL：拒绝，因为 temp 代码未完成、命名空间不统一，且未经过当前构建验证。

### Decision: 保持 `intrusive_list` 为显式侵入式链表基础设施

`ktl::intrusive_list` 应继续作为调用方持有节点的侵入式/list-foundation 组件。API 命名或文档必须明确 `erase` 只 unlink，不销毁也不释放节点。

理由：现有内存管理代码手动从内核内存分配 list node，并依赖可预测的节点生命周期。

备选方案：

- 转成完全 owning container：暂不采用，因为 allocator 支撑尚不成熟，且现有调用点使用侵入式语义。
- 只暴露裸节点函数：拒绝，因为当前 iterator/list wrapper 已有价值且已被采用。

### Decision: 命名迁移一步到位且不保留旧名 alias

本 change 将公开命名统一为 `ktl::bitset`、`ktl::buffer`、`ktl::intrusive_list`、`ktl::intrusive_list_node` 和 `ktl::pair`。实现时必须直接迁移 `mm`、`kernel` 和文档中的调用点，不增加 `using Buffer = buffer`、`using klist = intrusive_list` 或 `using klist_node = intrusive_list_node` 这类旧名 alias。

理由：KTL 仍处于早期阶段，旧名数量少且调用点集中。保留 alias 会延长命名混乱状态，使文档和调用点同时存在两套 API 名称。

备选方案：

- 保留旧名 alias 作为过渡：拒绝，因为用户明确要求不使用 alias，且当前调用点可以一次性迁移。
- 继续使用 `klist`/`klist_node`：拒绝，因为名称不能表达侵入式节点所有权，容易被误解为 owning list。
- 将 `intrusive_list` 简化为 `list`：拒绝，因为当前实现不管理节点内存和元素生命周期，叫 `list` 容易误导使用者。

### Decision: owning 容器必须建立在 allocator 支撑之后

拥有节点生命周期的 list/map/tree 容器必须依赖小型 KTL allocator 接口，可由 `kmalloc/free` 或调用方提供的分配策略支撑。

理由：`temp/bigos_` 中 list/map/tree 展示了方向，但 TODO 分配路径是主要阻塞点。没有 allocator 语义就加入 owning 容器会形成不安全的半成品。

备选方案：

- owning 容器直接调用 `kmalloc`：可行但复用性较差，也更难测试。
- 延后所有 map/tree 工作：更安全，但无法回应本次补齐 temp 能力的目标。

### Decision: 分阶段补齐缺失能力

新增能力按以下顺序推进：

1. 核心工具：必要时提供 move/forward 辅助、简单算法、比较辅助和 pair 增强。
2. 分配基础：提供 allocator traits/helpers，并明确分配失败行为。
3. 适配器：在稳定 list/heap primitives 之上提供 queue 和 priority queue。
4. 有序结构：红黑树 foundation 和 map 必须在旋转、迭代、插入、删除和不变量验证覆盖后再进入活动 API。

理由：这样可以在不继承 `temp` 全部复杂度的情况下，让活动内核逐步获得有用能力。

备选方案：

- 先实现 map：拒绝，因为它依赖 allocator 和 tree 正确性。
- 只文档化缺失能力而不实现：拒绝，因为需求要求在合理范围内补齐 temp 中已有但当前缺失的能力。

### Decision: 文档是 API 契约的一部分

每个 KTL 组件都必须有文档说明用途、头文件路径、命名空间、所有权规则、分配行为、失败行为、迭代器失效或修改规则，以及至少一个使用示例。

理由：KTL 组件处于低层，误用成本高。组件文档可以减少内存管理和驱动代码中的后续误用。

备选方案：

- 只在头文件中写注释：拒绝，因为组件级用法和约束超出了内联注释范围。

## Risks / Trade-offs

- `bitset` 语义修正可能暴露 slab 既有隐患 -> 缓解：审查 `Slab::alloc_obj/free_obj`，测试 scan/set/reset 组合，并显式处理分配失败。
- `intrusive_list` 命名和 API 收紧需要修改调用点 -> 缓解：在同一实现阶段直接迁移 `mm`、`kernel` 和文档，不提供旧名 alias，依靠构建暴露遗漏调用点。
- 新 allocator-backed 容器可能超出当前需求 -> 缓解：只实现最小必要接口，不追求高级 STL 兼容。
- 红黑树/map 正确性风险高 -> 缓解：拆分 tree foundation 与 map API，在内核调用点采用前验证旋转、迭代、插入、删除不变量。
- clang/clangd 在 freestanding 配置下可能与交叉 GCC 表现不一致 -> 缓解：记录 flags、差异和误报；以 xmake 交叉构建作为主要验证信号。
- 本地可能缺少 Bochs 或交叉工具链 -> 缓解：明确记录无法执行的验证、原因和剩余风险。

## Migration Plan

1. 为当前 KTL 行为补充可行的聚焦检查。
2. 先完成 KTL 公开命名迁移：`Buffer` -> `buffer`，`klist` -> `intrusive_list`，`klist_node` -> `intrusive_list_node`，并同步迁移 `mm`、`kernel` 调用点。
3. 删除或更新文档中的旧名描述，不保留旧名 alias 或旧名用法示例。
4. 以最小 API 破坏修复现有 `bitset` 和 `buffer` 缺陷。
5. 收紧 `intrusive_list` 构造、拷贝和生命周期语义，同时保持现有内存管理行为可用。
6. 添加 allocator 与 algorithm utility 基础。
7. 添加 queue/priority queue adapters，并记录其存储行为。
8. 分阶段添加红黑树 foundation 和 map，并以不变量验证作为进入活动 API 的前置条件。
9. 每个组件公共接口稳定后更新 `docs/en/ktl`。
10. 工具链可用时运行 xmake 交叉构建；不可用时记录缺失工具链，并运行尽可能贴近 freestanding C++17 的辅助诊断。

回滚策略：按 header/source 隔离组件变更，在验证前不让新增组件进入无关子系统。如果新增组件风险过高，可保持未引用或移除，不影响现有 KTL 用户。

## Resolved Decisions

- owning 容器不隐藏依赖全局 `kmalloc/free`。KTL 提供默认 `ktl::allocator<T>`，但 allocator-backed 容器必须显式暴露 allocator 策略，并文档化其可用阶段；早期启动路径优先使用 intrusive 或调用方提供存储的组件。
- `bitset::scan` 保持 unsigned index 返回风格，但定义 `ktl::bitset::npos` 作为唯一失败值。调用方必须在使用返回值前检查 `npos`，不得继续依赖 `return -1` 的隐式转换。
- `ktl::buffer` 以调用方提供存储作为默认安全模式。隐藏 `kmalloc` 分配不是默认推荐路径；若保留内部申请能力，必须显式标记 ownership、失败状态和释放策略。
- `map`/`rb_tree` 不预设固定首个真实消费者，也不因 buddy、slab 或 early-mm 等关键路径而默认禁止使用。只要具体场景需要有序关联结构，且实现已通过不变量、分配失败、初始化阶段和调用点行为验证，就可以在对应内核子系统中采用。
