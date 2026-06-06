# KTL 组件文档

KTL 是 BigOS 的 freestanding C++17 内核工具层。公共组件必须避免异常、RTTI、线程、文件、socket、hosted STL 容器和隐藏 OS 依赖；早期启动路径优先使用调用方提供存储或侵入式结构。

## 组件选择

| 组件 | 头文件 | 状态 | 适用场景 |
| --- | --- | --- | --- |
| `ktl::bitset` | `<ktl/bitset.h>` | 活动 | allocator/slab 中的对象占用位图 |
| `ktl::buffer` | `<ktl/buffer.h>` | 活动 | 字节环形缓冲区，优先使用调用方存储 |
| `ktl::intrusive_list` | `<ktl/list.h>` | 活动 | 调用方持有节点和对象生命周期的双向链表 |
| `ktl::intrusive_list_node` | `<ktl/list.h>` | 活动 | 侵入式链表节点和值存储 |
| `ktl::pair` | `<ktl/pair.h>` | 活动 | 小型 key/value 或 tuple-like 值传递 |
| `ktl::allocator` | `<ktl/allocator.h>` | 活动 | owning KTL 容器的最小分配策略 |
| `ktl::queue` | `<ktl/queue.h>` | 活动 | 调用方提供固定容量存储的 FIFO 队列 |
| `ktl::priority_queue` | `<ktl/priority_queue.h>` | 活动 | 调用方提供固定容量存储的堆式优先队列 |
| `ktl::rb_tree` | `<ktl/rb_tree.h>` | 活动基础 | allocator-backed 有序树 foundation |
| `ktl::map` | `<ktl/map.h>` | 活动基础 | 无异常 key/value 查找、插入、删除和有序迭代 |
| `ktl::less`、`ktl::swap`、`ktl::min`、`ktl::max` | `<ktl/algorithm.h>` | 活动 | KTL 容器内部和低层调用点的基础算法 |

## `ktl::bitset`

- 构造：`bitset(bits, storage)` 使用调用方提供的 bitmap 存储；`bitset(bits)` 通过 `bigos::kmalloc` 申请内部存储。
- 初始化：构造函数会将 bitmap 存储清零，初始状态为全部 bit 空闲。
- 计数：`set()`/`reset()` 只按实际变化的 bit 更新 `set_size()` 和 `reset_size()`。
- 失败：`scan(len)` 找不到连续空闲区间时返回 `ktl::bitset::npos`，调用方必须在作为索引前检查。
- 边界：越界 `set`、`reset` 和 `test` 不读写非法内存，按 no-op 或 `false` 返回。

```cpp
uint8_t bitmap[(64 + 7) / 8];
ktl::bitset bits(64, bitmap);
uint32_t index = bits.scan(1);
if (index != ktl::bitset::npos)
    bits.set(index);
```

## `ktl::buffer`

- 存储：`buffer(cap, storage)` 借用调用方存储；`buffer(cap)` 使用 `bigos::kmalloc` 申请自有存储，并在析构时释放。
- 失败：零容量或分配失败会让 `valid()` 返回 `false`；读写返回 `0`，不移动 `head`/`tail`。
- 读写：空读返回 `0`；满写返回 `0`；批量读写只处理可用空间或可读数据量。
- 限制：不保证并发安全；中断上下文使用前必须由调用方处理同步。

```cpp
uint8_t storage[128];
ktl::buffer buf(sizeof(storage), storage);
buf.write(static_cast<uint8_t>('A'));
uint8_t value = buf.read();
```

## `ktl::intrusive_list`

- 所有权：链表只维护链接关系，不释放节点，也不拥有节点背后的内存。
- 节点：`ktl::intrusive_list_node<T>` 在节点内部构造 `T`，节点析构时销毁 `T`。
- 修改：`insert` 将已有节点挂入链表；`erase` 和 `remove` 只 unlink 节点。
- 拷贝：链表和节点禁止拷贝，避免复制链接状态；链表支持移动基础设施。
- 生命周期：调用方必须保证节点在 unlink 前保持有效，且不得把一个已链接节点重复插入另一个链表。

```cpp
ktl::intrusive_list<int> list;
auto *node = new ktl::intrusive_list_node<int>(7);
list.insert(node);
auto it = list.begin();
list.erase(it);
delete node;
```

## `ktl::pair` 和算法

- `ktl::pair<T1, T2>` 提供默认构造、拷贝、赋值和字典序比较。
- `ktl::make_pair(x, y)` 返回 `ktl::pair<T1, T2>`，不依赖 hosted STL。
- `<ktl/algorithm.h>` 提供 `ktl::less`、`ktl::swap`、`ktl::min` 和 `ktl::max`。

```cpp
auto item = ktl::make_pair<ptr_t, uint32_t>(physical_base, nr_pages);
if (item.second != 0) {
    // caller owns the pointed memory and its lifetime.
}
```

## Allocator 和 Owning 容器

- `ktl::allocator<T>` 是最小策略接口，使用 `bigos::kmalloc/free`，失败时返回 `nullptr`。
- `ktl::queue<T>` 和 `ktl::priority_queue<T>` 使用调用方提供的固定容量数组，不隐藏分配。
- `ktl::rb_tree` 和 `ktl::map` 使用 allocator-backed 节点，插入失败通过 `nullptr` 或 `false` 暴露。
- early-mm 和启动关键路径应优先使用侵入式链表或调用方存储组件，只有确认 `kmalloc` 可用后再使用 allocator-backed 容器。

```cpp
int storage[4];
ktl::queue<int> q(storage, 4);
q.push(1);
q.push(2);
int out = 0;
q.pop(&out);
```

## 兼容性与限制

- KTL 不提供旧名 alias；活动公开 API 以本文组件列表为准。
- `bitset::scan` 失败返回 `ktl::bitset::npos`，调用方必须先检查再使用索引。
- `temp/bigos_` 不是活动 API，仅作为历史参考。

## 不支持或实验能力

- 不支持完整 ISO C++ STL 替代品。
- 不支持隐藏全局分配的动态数组、字符串、线程安全容器或异常驱动错误处理。
- `temp/bigos_` 中未完成、存在 TODO 或命名空间混杂的实验代码不是活动 API。
