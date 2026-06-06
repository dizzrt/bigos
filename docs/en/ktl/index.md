# KTL Component Documentation

KTL is BigOS' freestanding C++17 kernel utility layer. Public components must avoid exceptions, RTTI, threads, files, sockets, hosted STL containers, and hidden OS dependencies. Early boot paths should prefer caller-provided storage or intrusive structures.

## Component Selection

| Component | Header | Status | Use Case |
| --- | --- | --- | --- |
| `ktl::bitset` | `<ktl/bitset.h>` | Active | Object occupancy bitmap in allocators/slab |
| `ktl::buffer` | `<ktl/buffer.h>` | Active | Byte ring buffer, preferably with caller-provided storage |
| `ktl::intrusive_list` | `<ktl/list.h>` | Active | Doubly linked list where caller owns node and object lifetime |
| `ktl::intrusive_list_node` | `<ktl/list.h>` | Active | Intrusive list node and value storage |
| `ktl::pair` | `<ktl/pair.h>` | Active | Small key/value or tuple-like value passing |
| `ktl::allocator` | `<ktl/allocator.h>` | Active | Minimal allocation policy for owning KTL containers |
| `ktl::queue` | `<ktl/queue.h>` | Active | FIFO queue backed by caller-provided fixed-capacity storage |
| `ktl::priority_queue` | `<ktl/priority_queue.h>` | Active | Heap-based priority queue backed by caller-provided fixed-capacity storage |
| `ktl::rb_tree` | `<ktl/rb_tree.h>` | Active foundation | Allocator-backed ordered tree foundation |
| `ktl::map` | `<ktl/map.h>` | Active foundation | Exception-free key/value lookup, insertion, removal, and ordered iteration |
| `ktl::less`, `ktl::swap`, `ktl::min`, `ktl::max` | `<ktl/algorithm.h>` | Active | Basic algorithms for KTL containers and low-level call sites |

## `ktl::bitset`

- Construction: `bitset(bits, storage)` uses caller-provided bitmap storage; `bitset(bits)` allocates internal storage through `bigos::kmalloc`.
- Initialization: constructors clear bitmap storage, so all bits start free.
- Counting: `set()` / `reset()` update `set_size()` and `reset_size()` only when a bit actually changes.
- Failure: `scan(len)` returns `ktl::bitset::npos` when no contiguous free range exists; callers must check before using it as an index.
- Bounds: out-of-range `set`, `reset`, and `test` do not access invalid memory and act as no-op or return `false`.

```cpp
uint8_t bitmap[(64 + 7) / 8];
ktl::bitset bits(64, bitmap);
uint32_t index = bits.scan(1);
if (index != ktl::bitset::npos)
    bits.set(index);
```

## `ktl::buffer`

- Storage: `buffer(cap, storage)` borrows caller storage; `buffer(cap)` allocates owned storage with `bigos::kmalloc` and frees it in the destructor.
- Failure: zero capacity or allocation failure makes `valid()` return `false`; reads and writes return `0` and do not move `head`/`tail`.
- Reads/writes: empty reads return `0`; full writes return `0`; bulk operations process only available space or readable bytes.
- Limitation: no concurrency safety is guaranteed; interrupt-context users must provide synchronization.

```cpp
uint8_t storage[128];
ktl::buffer buf(sizeof(storage), storage);
buf.write(static_cast<uint8_t>('A'));
uint8_t value = buf.read();
```

## `ktl::intrusive_list`

- Ownership: the list stores links only. It does not free nodes and does not own the memory behind nodes.
- Nodes: `ktl::intrusive_list_node<T>` constructs `T` inside the node and destroys `T` when the node is destroyed.
- Mutation: `insert` links an existing node; `erase` and `remove` only unlink nodes.
- Copying: lists and nodes are non-copyable to avoid duplicating link state; list move infrastructure exists.
- Lifetime: callers must keep nodes valid until unlink and must not insert an already-linked node into another list.

```cpp
ktl::intrusive_list<int> list;
auto *node = new ktl::intrusive_list_node<int>(7);
list.insert(node);
auto it = list.begin();
list.erase(it);
delete node;
```

## `ktl::pair` And Algorithms

- `ktl::pair<T1, T2>` provides default construction, copy, assignment, and lexicographic comparison.
- `ktl::make_pair(x, y)` returns `ktl::pair<T1, T2>` without depending on hosted STL.
- `<ktl/algorithm.h>` provides `ktl::less`, `ktl::swap`, `ktl::min`, and `ktl::max`.

```cpp
auto item = ktl::make_pair<ptr_t, uint32_t>(physical_base, nr_pages);
if (item.second != 0) {
    // caller owns the pointed memory and its lifetime.
}
```

## Allocator And Owning Containers

- `ktl::allocator<T>` is a minimal policy interface using `bigos::kmalloc/free`; allocation failure returns `nullptr`.
- `ktl::queue<T>` and `ktl::priority_queue<T>` use caller-provided fixed-capacity arrays and hide no allocation.
- `ktl::rb_tree` and `ktl::map` use allocator-backed nodes; insertion failure is exposed through `nullptr` or `false`.
- early-mm and boot-critical paths should prefer intrusive lists or caller-storage components, and should use allocator-backed containers only after `kmalloc` is known to be available.

```cpp
int storage[4];
ktl::queue<int> q(storage, 4);
q.push(1);
q.push(2);
int out = 0;
q.pop(&out);
```

## Compatibility And Limits

- KTL provides no legacy-name aliases. The active public API is the component list in this document.
- `bitset::scan` failure returns `ktl::bitset::npos`; callers must check before using the result as an index.
- `temp/bigos_` is not an active API and is only historical reference material.

## Unsupported Or Experimental Capabilities

- KTL does not provide a full ISO C++ STL replacement.
- KTL does not support dynamically sized arrays, strings, thread-safe containers, or exception-driven error handling that hide global allocation.
- Incomplete, TODO-bearing, or namespace-mixed experimental code under `temp/bigos_` is not an active API.
