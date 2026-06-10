## ADDED Requirements

### Requirement: Kernel block device write API

BigOS SHALL 在现有只读块读接口之上新增一个内核块设备写接口，把整扇区从调用方缓冲写入某 LBA 地址。`BlockDevice` MUST 以追加写入口（如 `write_impl`）暴露写能力，`write_impl` 为空的设备 MUST 被视为只读；写 API MUST 在发起设备写前校验扇区数、源缓冲长度与算术溢出，且 MUST NOT 改变现有只读读路径、扇区大小或 MBR/exFAT 发现契约。

#### Scenario: Aligned sector write succeeds

- **WHEN** 内核对一个具备写后端、源缓冲足够容纳 `sector_count * sector_size` 的可写块设备请求写一个或多个扇区
- **THEN** 块设备写 API MUST 把请求扇区写入底层设备并返回成功

#### Scenario: Read-only device rejects write

- **WHEN** 内核对一个 `write_impl` 为空（只读）的块设备请求写扇区
- **THEN** 块设备写 API MUST 返回确定性的 Unsupported 状态（由上层映射为 `-EROFS`），MUST NOT 发起设备写

#### Scenario: Write buffer or LBA validation fails

- **WHEN** 写请求的源缓冲小于所需字节数，或扇区范围的末端 LBA 计算溢出
- **THEN** 块设备写 API MUST 在发起设备 I/O 前拒绝请求并返回有界错误状态

#### Scenario: Device write error is deterministic

- **WHEN** ATA PIO 写在轮询/写数据/flush 阶段未达预期状态或超时
- **THEN** 后端 MUST 停止轮询并返回设备超时或设备错误状态而不无限挂起，且 MUST NOT 破坏后续读路径或扇区大小契约

### Requirement: Block write context boundary

块设备写 SHALL 仅在端口 I/O 与内存管理初始化后的可阻塞普通内核上下文调用。写 API MUST NOT 被标注为 IRQ-handler-safe、preemption-safe、异步或可在不可阻塞上下文调用。

#### Scenario: Non-IRQ context write

- **WHEN** 内核在可阻塞普通上下文（如经块缓冲缓存落盘）调用块写 API
- **THEN** 该调用在支持的上下文边界内，MAY 执行同步轮询写 I/O

#### Scenario: IRQ context write is out of scope

- **WHEN** 代码试图把块写 API 当作 IRQ-handler-safe 行为使用
- **THEN** 规范不保证正确性，实现文档 MUST 标注该用法不支持
