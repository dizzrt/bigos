## 1. 基线梳理

- [x] 1.1 盘点 `cpp/include/ktl` 和 `cpp/ktl` 下的活动 KTL 头文件与源文件，记录当前公共 API 和调用点。
- [x] 1.2 审查 `temp/bigos_` 中相关 KTL 文件，将每项能力分类为可复用概念、需要重写的候选项或不应支持的实验项。
- [x] 1.3 识别当前 buddy、slab、kmalloc、vmem 和 kernel 中依赖 `ktl::bitset`、`ktl::intrusive_list`、`ktl::intrusive_list_node`、`ktl::buffer` 或 `ktl::pair` 的调用点。
- [x] 1.4 在编辑头文件前记录最终命名策略：采用 `ktl::bitset`、`ktl::buffer`、`ktl::intrusive_list`、`ktl::intrusive_list_node`、`ktl::pair`，且不提供旧名 alias。

## 2. 命名迁移

- [x] 2.1 将 `ktl::Buffer` 重命名为 `ktl::buffer`，并调整对应头文件、源文件、构造函数和声明。
- [x] 2.2 将 `ktl::klist` 重命名为 `ktl::intrusive_list`，将 `ktl::klist_node` 重命名为 `ktl::intrusive_list_node`，并调整类型声明和相关内部命名。
- [x] 2.3 直接迁移 `mm`、`kernel` 和其他当前调用点到新 KTL 名称。
- [x] 2.4 确认没有添加 `Buffer`、`klist` 或 `klist_node` 的旧名 alias、typedef 或兼容 wrapper。
- [x] 2.5 删除或更新文档、注释和示例中的旧名描述，确保只出现活动新命名。

## 3. 现有组件加固

- [x] 3.1 修复 `ktl::bitset` 对调用方提供存储和内部申请 bitmap 存储的初始化语义。
- [x] 3.2 修复 `ktl::bitset::set` 和 `ktl::bitset::reset` 在单 bit、部分字节、整字节、区间和整个位图操作中的计数逻辑。
- [x] 3.3 修复 `ktl::bitset::scan` 的字节寻址、读前边界检查和失败返回值文档化。
- [x] 3.4 更新 slab 分配调用点，在设置对象 bit 或构造对象头前检查 bitmap scan 失败。
- [x] 3.5 确保 slab 创建的 bitmap 内存按新的 `bitset` 契约完成初始化。
- [x] 3.6 修复 `ktl::buffer` 在空读、部分读、满 buffer、零容量和分配失败场景下的批量读写状态变化。
- [x] 3.7 明确 `ktl::buffer` 借用存储与自有存储的行为，并实现清理逻辑或显式记录永久分配语义。
- [x] 3.8 在不破坏当前侵入式链表用户的前提下，加固 `ktl::intrusive_list` 和 `ktl::intrusive_list_node` 的拷贝、移动和对象生命周期行为。
- [x] 3.9 改进 `ktl::pair` 的构造、比较和 `make_pair` 辅助函数，同时保持 freestanding 兼容。

## 4. 缺失 KTL 能力补齐

- [x] 4.1 添加或整合 KTL 组件需要的 freestanding-safe 基础算法工具。
- [x] 4.2 添加适用于 owning KTL 容器和内核分配失败行为的最小 allocator foundation。
- [x] 4.3 添加 FIFO queue 能力，并文档化其后端存储、所有权和失败行为。
- [x] 4.4 添加 priority queue 能力，并文档化 comparator、容量/分配模型和排序行为。
- [x] 4.5 在明确节点所有权、迭代器行为、插入、删除和不变量检查后，实现红黑树 foundation。
- [x] 4.6 在已验证的 tree foundation 之上实现 map 抽象，提供无异常 lookup、insert、erase 和 iteration 行为。
- [x] 4.7 将不支持或风险过高的 `temp/bigos_` 概念排除在活动 KTL 外，并记录原因。

## 5. 文档

- [x] 5.1 创建 `docs/en/ktl/index.md`，包含组件列表、选型指导、活动/实验状态和 freestanding 约束。
- [x] 5.2 文档化 `ktl::bitset`，包括初始化、scan 失败、计数语义、slab 用法和示例。
- [x] 5.3 文档化 `ktl::buffer`，包括自有/借用存储、容量规则、读写语义和示例。
- [x] 5.4 文档化 `ktl::intrusive_list` 和 `ktl::intrusive_list_node`，包括侵入式节点所有权、erase/remove 行为、迭代器使用和示例。
- [x] 5.5 文档化 `ktl::pair` 和工具函数，包括构造、比较和 freestanding 限制。
- [x] 5.6 当 allocator、queue、priority queue、tree 和 map 成为活动公共 KTL API 后，为它们补充文档。
- [x] 5.7 为 KTL 行为变化添加迁移说明，并将 temp 启发的 API 映射到活动支持版本。
- [x] 5.8 确认 `docs/en/ktl` 不包含 `ktl::Buffer`、`ktl::klist` 或 `ktl::klist_node` 作为活动 API 的旧名说明或示例。

## 6. 验证

- [x] 6.1 为 `bitset` 的 set/reset/scan/count 行为添加聚焦的编译期或低层检查。
- [x] 6.2 为 `buffer` 的空/满/部分读写行为添加聚焦检查。
- [x] 6.3 为 `intrusive_list` 的插入、erase、remove、迭代和节点生命周期预期添加聚焦检查。
- [x] 6.4 在 queue、priority queue、tree 和 map 被内核子系统采用前，为它们添加聚焦检查。
- [x] 6.5 KTL 修改后，审查 buddy、slab、kmalloc 和 vmem 的初始化顺序、分配阶段、对象生命周期、对齐和失败行为。
- [x] 6.6 工具链可用时运行基于 `x86_64-elf-gcc` 的 `xmake`；不可用时记录交叉构建无法运行的原因。
- [x] 6.7 运行尽可能贴近 freestanding C++17 x86_64 kernel flags 的 clang 或 clangd 辅助诊断，或记录工具不可用和误报差异。
- [x] 6.8 如果 KTL 变更影响启动关键内存分配路径，执行或记录一次 Bochs smoke test 尝试。
- [x] 6.9 通过全仓搜索和构建确认不存在旧名调用点和旧名 alias。

## 7. 最终审查

- [x] 7.1 确认所有新增公共 KTL 头文件只在必要位置包含，且没有引入 hosted 依赖。
- [x] 7.2 确认没有引入异常、RTTI 假设、静态初始化风险或隐藏分配路径。
- [x] 7.3 确认 `docs/en/ktl` 与已实现 API 一致，且没有把 temp-only 代码描述为活动 API。
- [x] 7.4 使用验证结果和延期范围更新 OpenSpec 任务清单。

## 验证记录

- `x86_64-elf-g++ -std=c++17 -ffreestanding ... -fsyntax-only cpp/ktl/bitset.cc cpp/ktl/buffer.cc cpp/ktl/list.cc src/mm/slab.cc src/mm/buddy.cc src/mm/vmem.cc src/mm/kmem.cc`：通过。
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding ... -fsyntax-only cpp/ktl/bitset.cc cpp/ktl/buffer.cc cpp/ktl/list.cc src/mm/slab.cc src/mm/buddy.cc src/mm/vmem.cc src/mm/kmem.cc`：通过。
- `x86_64-elf-g++` 和 `clang++` 对包含 `algorithm`、`allocator`、`queue`、`priority_queue`、`rb_tree`、`map` 的聚焦头文件检查：通过。
- `xmake`：已运行，但当前全量构建在 `src/kernel/irq/isr.cc` 的既有 `irq_handler`/`MAX_IRQ_NUM` 诊断处失败，未到达链接和 Bochs smoke test。
- 旧名搜索：`ktl::Buffer`、`ktl::klist`、`ktl::klist_node`、旧名 alias/typedef 在活动代码和 `docs/en/ktl` 中未发现。
