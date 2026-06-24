## MODIFIED Requirements

### Requirement: 缓存写回与显式落盘
BigOS SHALL 以写回（write-back）语义管理脏块：写入只标脏，落盘 MUST 发生在显式 `fsync`/`sync`、缓存淘汰回写脏块、或显式全量同步时。落盘 MUST 经块 I/O 请求层提交写请求，且块 I/O 失败 MUST 以确定性错误返回并保持脏块不丢数据。块路径迁出同步轮询后，缓存仍 MUST 通过同步 request-layer wrapper 观察最终完成状态，MUST NOT 直接依赖设备同步轮询函数、IRQ 上下文写回或后台 writeback 来定义 durable success。

#### Scenario: 显式 sync 落盘脏块
- **WHEN** 调用方对一个或多个脏块请求落盘
- **THEN** 缓存 MUST 经块 I/O 请求层把脏块内容写到底层设备，成功后清除其 dirty 标记

#### Scenario: 落盘后淘汰仍可读回
- **WHEN** 一个块被写入、落盘、随后被淘汰出缓存，之后再次被读
- **THEN** 缓存 MUST 经块 I/O 请求层重新从底层设备装入并返回与落盘内容一致的数据

#### Scenario: 落盘块 IO 失败保留脏块
- **WHEN** 落盘时块 I/O 请求层返回设备错误、队列错误、timeout 或底层块设备写失败
- **THEN** 缓存 MUST 返回确定性 `-EIO` 或等价错误，MUST 保持该块 dirty、MUST NOT 丢弃数据，且 MUST NOT panic

#### Scenario: 非轮询 completion 成功后才能清 dirty
- **WHEN** cache writeback submits a dirty block through the migrated nonpolling block path
- **THEN** the cache MUST clear dirty state only after the request-layer wrapper observes terminal success
- **AND** it MUST treat pending, timeout, completion rejection, or device error as writeback failure

### Requirement: 缓存 IO 上下文边界
BigOS SHALL 仅在允许阻塞、分配与同步块 IO wrapper 的进程上下文执行缓存装入与落盘。缓存装入/落盘 MUST 经块 I/O 请求层提交请求，并且 MUST NOT 在 IRQ 上下文、调度临界区、preemption-disable 的不可阻塞区或其他不可阻塞上下文进行。块路径迁出同步轮询后，该边界仍保持不变：IRQ/completion 路径只允许完成已 pending 的块请求，不允许执行缓存策略或文件系统写回。

#### Scenario: 可阻塞上下文执行装入/落盘
- **WHEN** 缓存装入或落盘从允许阻塞的普通进程上下文被调用
- **THEN** 缓存 MAY 分配内核对象并经块 I/O 请求层执行同步 wrapper 上的块设备 IO

#### Scenario: 不可阻塞上下文拒绝落盘
- **WHEN** 缓存装入或落盘从 IRQ、调度临界区或 preemption-disable 的不可阻塞上下文被调用
- **THEN** BigOS MUST 确定性失败或进入文档化诊断路径，MUST NOT 向请求层提交会执行阻塞等待的块 IO 请求

#### Scenario: completion 不触发 cache policy
- **WHEN** a migrated block request completes from IRQ context or an IRQ-like producer
- **THEN** BigOS MUST only wake the waiter and publish the final request status
- **AND** it MUST NOT run cache eviction, dirty scanning, filesystem synchronization, or persistent metadata commit from that completion context

## ADDED Requirements

### Requirement: cache 验证覆盖非轮询底层
BigOS SHALL validate that page/buffer cache behavior remains stable when the underlying block request completes through the migrated nonpolling path. Validation MUST cover read miss load, writeback success, writeback failure dirty retention, dirty victim eviction, and persistent `/rw` clean-sync where emulator disk support is available.

#### Scenario: cache round-trip 经非轮询完成
- **WHEN** cache validation writes a block, marks it dirty, synchronizes it through the migrated block path, evicts it, and reads it again
- **THEN** the reloaded data MUST match the successfully synchronized content
- **AND** validation MUST observe the request-layer final status rather than bypassing the block layer

#### Scenario: writeback failure 仍保留 dirty
- **WHEN** validation injects or observes a deterministic writeback failure through the migrated block request path
- **THEN** the cache MUST keep the affected block dirty or pending-writeback
- **AND** validation MUST NOT report sync, fsync, or eviction success for that block

#### Scenario: persistent clean-sync 回归
- **WHEN** persistent `/rw` clean-sync validation runs on an emulator setup with the required writable backing disk
- **THEN** it MUST continue to pass with the migrated block path
- **AND** if the emulator or disk setup is unavailable, validation notes MUST record the skipped clean-sync coverage and residual risk
