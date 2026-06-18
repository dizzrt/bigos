## Purpose

定义 BigOS 内核块缓冲缓存能力：以（块设备, 块号）为键缓存固定大小块的脏/干净状态，
提供读命中/未命中装入、写入标脏、写回与显式 `sync`/`fsync` 落盘、有界容量与确定性淘汰，
并严格限定缓存装入与落盘只在允许阻塞与同步块 IO 的进程上下文进行。该能力不引入 SMP
缓存一致性与锁，不改变现有只读读路径与扇区大小契约，并以默认关闭的运行时 smoke 验证。
## Requirements
### Requirement: 块缓冲缓存以（设备, 块号）为键

BigOS SHALL 提供一个内核块缓冲缓存，以（块设备指针, 块号）为键缓存固定大小块（块大小为扇区大小的固定倍数），并维护每块的有效（valid）、脏（dirty）与引用计数状态。缓存 MUST 提供获取/释放/标脏/落盘接口，读路径 MUST 优先命中缓存、未命中时经现有块读路径装入，且 MUST NOT 改变现有只读读路径与扇区大小契约。

#### Scenario: 读命中返回缓存块

- **WHEN** 内核以某（设备, 块号）请求块且该块已在缓存中有效
- **THEN** 缓存 MUST 直接返回该缓存块，MUST NOT 再次发起底层块设备读

#### Scenario: 读未命中装入

- **WHEN** 内核请求一个不在缓存中的（设备, 块号）且有空闲或可淘汰槽位
- **THEN** 缓存 MUST 分配一块、经现有块设备读路径装入数据、标记为有效，并返回该块

#### Scenario: 写入标脏对后续读可见

- **WHEN** 调用方把数据写入某缓存块并标脏
- **THEN** 后续对同一（设备, 块号）的读 MUST 看到写入后的内容
- **AND** 在未落盘前该块 MUST 保持 dirty 状态

### Requirement: 缓存写回与显式落盘

BigOS SHALL 以写回（write-back）语义管理脏块：写入只标脏，落盘 MUST 发生在显式 `fsync`/`sync`、缓存淘汰回写脏块、或显式全量同步时。落盘 MUST 经块设备写路径，且块 IO 失败 MUST 以确定性错误返回并保持脏块不丢数据。

#### Scenario: 显式 sync 落盘脏块

- **WHEN** 调用方对一个或多个脏块请求落盘
- **THEN** 缓存 MUST 经块设备写路径把脏块内容写到底层设备，成功后清除其 dirty 标记

#### Scenario: 落盘后淘汰仍可读回

- **WHEN** 一个块被写入、落盘、随后被淘汰出缓存，之后再次被读
- **THEN** 缓存 MUST 重新从底层设备装入并返回与落盘内容一致的数据

#### Scenario: 落盘块 IO 失败保留脏块

- **WHEN** 落盘时底层块设备写返回设备错误
- **THEN** 缓存 MUST 返回确定性 `-EIO`，MUST 保持该块 dirty、MUST NOT 丢弃数据，且 MUST NOT panic

### Requirement: 有界容量与确定性淘汰

BigOS SHALL 以固定有界容量管理缓存。容量耗尽时缓存 MUST 优先淘汰引用计数为零的干净块；当只剩脏块可淘汰时 MUST 先回写脏块再复用；无任何可淘汰块时 MUST 以确定性错误（如 `-ENOMEM`）返回而不死等阻塞。

#### Scenario: 优先淘汰干净块

- **WHEN** 缓存已满且存在引用计数为零的干净块，又需为新块腾出空间
- **THEN** 缓存 MUST 选择一个干净、未被引用的块复用，MUST NOT 因此触发落盘

#### Scenario: 淘汰脏块前先回写

- **WHEN** 缓存已满且唯一可复用的块为脏块
- **THEN** 缓存 MUST 先把该脏块回写落盘再复用其槽位

#### Scenario: 无可淘汰块确定性失败

- **WHEN** 缓存已满且所有块都被引用、无法腾出空间
- **THEN** 缓存 MUST 返回确定性错误（如 `-ENOMEM`），MUST NOT 死等阻塞或 panic

### Requirement: 缓存 IO 上下文边界

BigOS SHALL 仅在允许阻塞、分配与同步块 IO 的进程上下文执行缓存装入与落盘。缓存装入/落盘 MUST NOT 在 IRQ 上下文、调度临界区、preemption-disable 的不可阻塞区或其他不可阻塞上下文进行。

#### Scenario: 可阻塞上下文执行装入/落盘

- **WHEN** 缓存装入或落盘从允许阻塞的普通进程上下文被调用
- **THEN** 缓存 MAY 分配内核对象并执行同步块设备 IO

#### Scenario: 不可阻塞上下文拒绝落盘

- **WHEN** 缓存装入或落盘从 IRQ、调度临界区或 preemption-disable 的不可阻塞上下文被调用
- **THEN** BigOS MUST 确定性失败或进入文档化诊断路径，MUST NOT 发起阻塞块 IO 或落盘

### Requirement: 块缓冲缓存验证可复现

BigOS SHALL 通过默认关闭的运行时 smoke 与源码/行为断言验证块缓冲缓存。验证 MUST 记录工具链与模拟器可用性、串口 marker、跳过的用例与残余风险，且默认启动 marker 与既有 smoke 矩阵 MUST 保持不变。

#### Scenario: 缓存 smoke 发射有界 marker

- **WHEN** 启用缓存相关验证开关并在模拟器中启动
- **THEN** 验证 MUST 覆盖「写后读回一致」「落盘后淘汰再读一致」「容量耗尽确定性失败」并发射确定性 COM1 marker
- **AND** QEMU/Bochs、交叉工具链、ROM/显示或磁盘镜像不可用时 MUST 记录为跳过验证而非声称通过

### Requirement: 持久后端缓存同步跨重启可验证
BigOS SHALL make page/buffer cache write-back for a persistent writable backend durable enough for clean-reboot validation. A successful `fsync`, explicit sync, or eviction write-back on persistent `/rw` MUST write all required dirty data and metadata blocks to the underlying writable block device before reporting success. The cache MUST retain the existing process-context-only boundary for load and write-back and MUST NOT perform blocking persistent I/O from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: fsync 成功后 clean reboot 读回
- **WHEN** a persistent `/rw` file has dirty data and metadata blocks and the caller invokes `fsync`
- **THEN** the cache MUST write the required blocks to the persistent block device before `fsync` reports success
- **AND** after a clean reboot and remount, reading the file MUST return the synchronized content

#### Scenario: 淘汰脏块成功后持久介质可读
- **WHEN** cache pressure evicts an unreferenced dirty block belonging to persistent `/rw` and the write-back succeeds
- **THEN** the cache MAY reuse the slot
- **AND** later reload from the persistent block device MUST observe the written block content

#### Scenario: 不可阻塞上下文拒绝持久写回
- **WHEN** persistent cache load or write-back is attempted from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O or publish a successful persistent commit from that context

### Requirement: 持久写回失败保留脏状态
BigOS SHALL keep dirty cache state explainable when persistent write-back fails. If a persistent block device write returns an error or timeout, the cache MUST return a deterministic error, MUST NOT mark the affected block clean, and MUST NOT report `fsync` or sync success for the affected filesystem state.

#### Scenario: 块设备写失败不清除 dirty
- **WHEN** persistent cache write-back fails because the underlying writable block device reports an error or timeout
- **THEN** the cache MUST keep the affected block dirty or otherwise preserve an explainable pending-write state
- **AND** it MUST return a deterministic error to the caller

#### Scenario: 同步失败不扩大持久性承诺
- **WHEN** `fsync` or explicit sync returns an error for persistent `/rw`
- **THEN** BigOS MUST NOT claim the attempted update survives reboot
- **AND** previously synchronized filesystem state MUST remain explainable according to the non-journaled persistent filesystem boundary

### Requirement: 缓存支持文件增长和截断一致性
BigOS SHALL make page/buffer cache state consistent with `/rw` file growth and truncate operations. Newly allocated or zero-read file ranges MUST be represented so later reads return committed data or zero bytes as required. Blocks dirtied by extension writes or truncate metadata updates MUST remain dirty until successfully written back or otherwise synchronized according to the existing cache contract.

#### Scenario: 扩展写后缓存读命中返回新内容
- **WHEN** a `/rw` regular file extension write updates one or more cached blocks and marks them dirty
- **THEN** later reads through the cache MUST return the written content for the committed range
- **AND** unwritten gap ranges MUST return zero bytes

#### Scenario: 截断后缓存不返回旧尾部数据
- **WHEN** a `/rw` regular file is successfully truncated to a smaller size
- **THEN** cache reads beyond the new EOF MUST NOT return stale tail data from blocks formerly owned by that file
- **AND** dirty or clean cached blocks that no longer belong to the file MUST be invalidated, remapped, or made unreachable from that file

### Requirement: 缓存写回失败不发布文件增长 durable success
BigOS SHALL keep cache write-back failure behavior explicit for file growth and truncate. If write-back of data blocks, inode metadata, free-space metadata, or directory-relevant metadata fails, the cache and filesystem MUST return a deterministic error and MUST NOT mark the affected growth or truncate state as durably committed.

#### Scenario: fsync 增长文件写回失败
- **WHEN** `fsync` attempts to write dirty blocks for a grown `/rw` regular file and the backing store reports an error
- **THEN** BigOS MUST return a deterministic write-back error
- **AND** it MUST preserve dirty or pending-write state instead of silently discarding the update

#### Scenario: 淘汰截断 metadata 失败
- **WHEN** cache eviction attempts to write metadata required for a truncate operation and the write-back fails
- **THEN** BigOS MUST keep the affected state explainable and report failure through the synchronization path
- **AND** it MUST NOT reuse the cache slot as though the metadata were durably written

### Requirement: 缓存块复用不泄漏释放文件数据
BigOS SHALL prevent stale data exposure when cache slots or backing blocks released by truncate or unlink are reused for file growth. Before a reused block becomes user-visible through another file or an extended range, BigOS MUST zero the visible unwritten bytes or fully overwrite them with committed data.

#### Scenario: 释放块被新文件复用
- **WHEN** a block formerly owned by one file is freed and later reused for another `/rw` file
- **THEN** cache-backed reads from the new file MUST NOT expose the old file's bytes
- **AND** the new file MUST observe either committed writes or zero-filled unwritten ranges

#### Scenario: cache slot 复用后键和内容匹配
- **WHEN** a cache slot previously associated with one file data block is reused for another device/block key
- **THEN** the cache MUST associate the slot with the new key and valid content before returning it to readers
- **AND** it MUST NOT return stale content under the new key

### Requirement: 缓存支持元数据 ordered commit
BigOS SHALL allow filesystem code to synchronize persistent `/rw` metadata dirty blocks according to an explicit ordered commit plan. The cache MUST support flushing selected dirty metadata blocks, preserving block identity by device and block number, propagating write-back errors, and keeping failed blocks dirty or pending. This requirement MUST retain the existing process-context-only boundary and MUST NOT perform blocking persistent I/O from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: 按提交计划同步 dirty metadata blocks
- **WHEN** persistent `/rw` submits an ordered metadata commit plan to the cache from a blocking process context
- **THEN** the cache MUST write the selected dirty blocks in the requested order or return a deterministic error at the failing block
- **AND** it MUST NOT report success before all required blocks in the plan are synchronized

#### Scenario: 不可阻塞上下文拒绝 metadata writeback
- **WHEN** metadata commit write-back is attempted from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O or publish durable metadata success

### Requirement: metadata 写回失败保留 dirty 或 pending 状态
BigOS SHALL keep cache state explainable when persistent metadata write-back fails. If a backing block write for a metadata block returns an error or timeout, the cache MUST return a deterministic error, MUST NOT mark the affected block clean, and MUST NOT let `fsync`, explicit sync, or cache eviction report durable success for the affected metadata commit.

#### Scenario: dirty metadata block 写失败
- **WHEN** the cache attempts to write a dirty metadata block for persistent `/rw` and the backing block device reports failure
- **THEN** the cache MUST keep that block dirty or pending
- **AND** it MUST return a deterministic write-back error to the caller

#### Scenario: eviction 不绕过 ordered commit
- **WHEN** cache pressure selects a dirty persistent metadata block that belongs to an ordered commit plan
- **THEN** eviction MUST either synchronize it according to the required ordering constraints or fail deterministically
- **AND** it MUST NOT reuse the cache slot as though unordered metadata write-back had completed successfully

