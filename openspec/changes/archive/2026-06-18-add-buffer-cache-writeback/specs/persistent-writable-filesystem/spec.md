## ADDED Requirements

### Requirement: 持久同步经缓存回写后才报告成功

BigOS SHALL make persistent `/rw` clean-sync success depend on successful page/buffer cache write-back of the required dirty file data and filesystem metadata blocks. A successful `fsync`, explicit synchronization, or clean validation boundary MUST NOT report durable success until the required dirty cache blocks for the persistent backing device have been written successfully. This requirement MUST NOT claim crash recovery, journal replay, power-loss safety, async I/O, or persistence for unsynchronized dirty state.

#### Scenario: fsync 成功扩大 clean-sync 承诺
- **WHEN** a process writes a bounded persistent `/rw` file, updates necessary metadata, and calls `fsync` from a blockable process context
- **THEN** BigOS MUST write the required dirty data and metadata cache blocks to the persistent backing device before `fsync` returns success
- **AND** after a clean reboot and remount with the same persistent test disk, reopening the file MUST return the synchronized content and metadata

#### Scenario: fsync 回写失败不声明持久成功
- **WHEN** persistent `/rw` `fsync` reaches a cache or backing-device write-back failure
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT claim that the attempted update survives a clean reboot

#### Scenario: 未同步 dirty 状态不扩大承诺
- **WHEN** persistent `/rw` contains dirty cache blocks that have not been successfully synchronized
- **THEN** BigOS MUST NOT describe those blocks as durable
- **AND** previously synchronized filesystem state MUST remain explainable within the bounded clean-sync contract

### Requirement: 持久缓存淘汰结果跨重载可观察

BigOS SHALL make successful dirty cache eviction for persistent `/rw` observable through later reloads from the persistent backing device. When eviction write-back succeeds, the cache MAY reuse the slot and later reads MUST reload the written content or metadata from the device. When eviction write-back fails, BigOS MUST keep the dirty or pending state and MUST NOT reuse the slot as if the state were durable.

#### Scenario: 淘汰写回成功后重载读回
- **WHEN** a persistent `/rw` dirty block is unreferenced, selected for eviction, and successfully written back
- **THEN** the cache MAY reuse the slot
- **AND** a later read that reloads the evicted block from the persistent backing device MUST observe the synchronized content

#### Scenario: 淘汰写回失败不发布 durable state
- **WHEN** persistent `/rw` dirty block eviction fails during backing-device write-back
- **THEN** BigOS MUST preserve dirty or pending state for that block
- **AND** it MUST NOT report the affected filesystem state as durably synchronized

### Requirement: 持久同步验证记录环境边界

BigOS SHALL provide default-off validation for persistent `/rw` cache-backed synchronization under the current x86_64 Legacy BIOS emulator path. Validation MUST cover successful `fsync`, clean reboot readback, eviction write-back readback, and deterministic failure or skipped/blocked reporting where backing toolchain, emulator, ROM/display, serial capture, or persistent disk dependencies are unavailable.

#### Scenario: 双启动验证缓存回写后的状态
- **WHEN** persistent synchronization validation runs with the required toolchain, emulator, serial capture, and persistent test disk available
- **THEN** the first run MUST write bounded files and metadata under persistent `/rw`, synchronize them through the cache write-back path, and stop at a clean validation boundary
- **AND** the second run MUST remount the same persistent test disk and verify the synchronized data and metadata

#### Scenario: 环境不可用时不声称通过
- **WHEN** xmake, the x86_64 cross toolchain, QEMU or Bochs, ROM/display support, serial capture, or the persistent test disk is unavailable
- **THEN** validation MUST record the missing dependency and residual risk as skipped or blocked
- **AND** it MUST NOT report runtime persistent synchronization validation as passed
