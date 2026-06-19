## MODIFIED Requirements

### Requirement: 块缓冲缓存以（设备, 块号）为键

BigOS SHALL 提供一个内核块缓冲缓存，以（块设备指针, 块号）为键缓存固定大小块（块大小为扇区大小的固定倍数），并维护每块的有效（valid）、脏（dirty）与引用计数状态。缓存 MUST 提供获取/释放/标脏/落盘接口，读路径 MUST 优先命中缓存、未命中时经块 I/O 请求层装入，且 MUST NOT 改变现有只读读路径与扇区大小契约。

#### Scenario: 读命中返回缓存块

- **WHEN** 内核以某（设备, 块号）请求块且该块已在缓存中有效
- **THEN** 缓存 MUST 直接返回该缓存块，MUST NOT 再次发起底层块设备读或块 I/O 请求

#### Scenario: 读未命中装入

- **WHEN** 内核请求一个不在缓存中的（设备, 块号）且有空闲或可淘汰槽位
- **THEN** 缓存 MUST 分配一块、经块 I/O 请求层提交读请求装入数据、标记为有效，并返回该块

#### Scenario: 写入标脏对后续读可见

- **WHEN** 调用方把数据写入某缓存块并标脏
- **THEN** 后续对同一（设备, 块号）的读 MUST 看到写入后的内容
- **AND** 在未落盘前该块 MUST 保持 dirty 状态

### Requirement: 缓存写回与显式落盘

BigOS SHALL 以写回（write-back）语义管理脏块：写入只标脏，落盘 MUST 发生在显式 `fsync`/`sync`、缓存淘汰回写脏块、或显式全量同步时。落盘 MUST 经块 I/O 请求层提交写请求，且块 I/O 失败 MUST 以确定性错误返回并保持脏块不丢数据。

#### Scenario: 显式 sync 落盘脏块

- **WHEN** 调用方对一个或多个脏块请求落盘
- **THEN** 缓存 MUST 经块 I/O 请求层把脏块内容写到底层设备，成功后清除其 dirty 标记

#### Scenario: 落盘后淘汰仍可读回

- **WHEN** 一个块被写入、落盘、随后被淘汰出缓存，之后再次被读
- **THEN** 缓存 MUST 经块 I/O 请求层重新从底层设备装入并返回与落盘内容一致的数据

#### Scenario: 落盘块 IO 失败保留脏块

- **WHEN** 落盘时块 I/O 请求层返回设备错误、队列错误或底层块设备写失败
- **THEN** 缓存 MUST 返回确定性 `-EIO` 或等价错误，MUST 保持该块 dirty、MUST NOT 丢弃数据，且 MUST NOT panic

### Requirement: 缓存 IO 上下文边界

BigOS SHALL 仅在允许阻塞、分配与同步块 IO 的进程上下文执行缓存装入与落盘。缓存装入/落盘 MUST 经块 I/O 请求层提交同步请求，并且 MUST NOT 在 IRQ 上下文、调度临界区、preemption-disable 的不可阻塞区或其他不可阻塞上下文进行。

#### Scenario: 可阻塞上下文执行装入/落盘

- **WHEN** 缓存装入或落盘从允许阻塞的普通进程上下文被调用
- **THEN** 缓存 MAY 分配内核对象并经块 I/O 请求层执行同步块设备 IO

#### Scenario: 不可阻塞上下文拒绝落盘

- **WHEN** 缓存装入或落盘从 IRQ、调度临界区或 preemption-disable 的不可阻塞上下文被调用
- **THEN** BigOS MUST 确定性失败或进入文档化诊断路径，MUST NOT 向请求层提交会执行阻塞块 IO 的请求

### Requirement: 设备范围 dirty block 同步
BigOS SHALL provide a bounded `sync_device()` or equivalent device-scoped page/buffer cache write-back path for dirty blocks belonging to a selected block device. The write-back path MUST write each selected dirty block through the block I/O request layer, MUST clear dirty state only after the request and underlying write succeed, MUST return a deterministic error for the first failed write, and MUST keep failed blocks dirty or pending. Existing global `sync_all()` MAY remain available as a debug or internal maintenance helper, but persistent `/rw` clean-sync success MUST be based on device-scoped or selected-block synchronization rather than an accidental global flush. This capability MUST remain process-context-only and MUST NOT introduce async I/O, request queues beyond the bounded synchronous request layer, new storage drivers, or SMP cache coherency.

#### Scenario: 设备同步成功清除 dirty
- **WHEN** a writable backend requests synchronization for dirty cache blocks belonging to its backing block device from a blockable process context
- **THEN** BigOS MUST submit write requests for the selected dirty blocks through the block I/O request layer to that block device
- **AND** it MUST clear each block's dirty state only after the corresponding request and device write succeed

#### Scenario: 设备同步失败保留 dirty
- **WHEN** device-scoped synchronization encounters a request-layer error, queue capacity error, backing block-device write error, or timeout
- **THEN** BigOS MUST return a deterministic write-back error to the caller
- **AND** it MUST keep the failed block dirty or otherwise represented as pending write-back state

#### Scenario: 设备同步不扩大无关设备承诺
- **WHEN** dirty cache blocks for multiple devices exist and the caller synchronizes one selected device
- **THEN** BigOS MUST NOT report durable success for dirty blocks belonging to other devices
- **AND** later documentation or validation MUST describe only the selected device's synchronized state as clean-sync eligible

#### Scenario: sync_all 保留为内部工具
- **WHEN** an internal diagnostic or maintenance path intentionally invokes global cache synchronization
- **THEN** BigOS MAY use the existing global helper for that internal purpose
- **AND** persistent writable filesystem success paths MUST still use device-scoped or selected-block synchronization to define their durable contract

### Requirement: dirty victim 淘汰写回不丢数据
BigOS SHALL make cache eviction of dirty unreferenced victims use the same request-layer write-back failure semantics as explicit synchronization. A dirty victim MAY be reused only after its write-back request and underlying device write succeed. If write-back fails, the cache MUST keep the victim associated with its original device/block key, MUST keep its dirty or pending state, and MUST return deterministic failure to the caller instead of silently reusing the slot.

#### Scenario: dirty victim 写回成功后复用
- **WHEN** the cache is full, an unreferenced dirty block is selected as the only reusable victim, and request-layer write-back succeeds
- **THEN** BigOS MAY reuse the cache slot for the newly requested device/block key
- **AND** a later reload of the old block from the backing device MUST observe the written content

#### Scenario: dirty victim 写回失败不复用
- **WHEN** the cache is full, an unreferenced dirty block is selected as a victim, and request-layer write-back fails
- **THEN** BigOS MUST keep the victim dirty and associated with its original device/block key
- **AND** it MUST fail the new cache request deterministically rather than returning a slot containing stale or uncommitted data
