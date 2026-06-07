# 内核块设备与 exFAT 读取路径

BigOS 阶段 7 新增内核运行期只读块设备与文件系统路径。该路径与 Legacy
BIOS bootloader 的 exFAT 辅助逻辑分离：bootloader 仍使用固定低地址缓冲区
和连续启动文件加载 `/boot/boot.bin` 与 kernel；内核运行期则在内存管理完成后
提供可复用的 bounded API。

## 范围

- 块读取使用同步只读 `BlockDevice` 契约，按完整 512-byte sector 读取到调用方
  自有缓冲区。
- 首个后端面向 Bochs raw image：ATA PIO primary master、LBA48、同步 polling
  与 bounded timeout。
- 文件系统层自动发现第一个有效 MBR exFAT 分区，校验 exFAT boot region，并
  挂载单个只读 volume。
- exFAT 支持绝对路径查找、普通文件元数据、bounded read、`NoFatChain` 连续
  文件，以及 bounded FAT-chain 跟随。
- API 仅支持普通内核上下文；不承诺 IRQ-handler-safe、异步、DMA、sleep 或
  SMP 语义。

## 非目标

- 不支持写入、删除、目录修改、权限、page cache 或完整 VFS。
- 不支持 AHCI、NVMe、USB storage、DMA、热插拔或中断驱动磁盘 I/O。
- 不实现受控只读子集之外的完整 exFAT 规范。
- 不修改 MBR/DBR/extended DBR 布局、BootInfo handoff、linker 地址或现有
  first user-program smoke。

## 验证 Smoke

`xmake f --fs_smoke=y` 启用默认关闭的 runtime smoke。镜像生成器会加入
`/boot/fs_smoke.txt`，payload 为 `BIGOS_FS_SMOKE_PAYLOAD\n`，同时保持现有
`/boot/boot.bin` 与 `kernel` 布局兼容。内核初始化期间，smoke 通过 ATA PIO
和 exFAT 读取该文件，并输出：

- 成功：`BIGOS_FS_EXFAT_READ_PASSED`。
- 失败：`BIGOS_FS_EXFAT_READ_FAILED code=<code>`，覆盖 mount、lookup、read
  或 payload 校验失败。

本阶段是阶段 8 `load-user-elf-program` 的 API 前置条件；后续用户 ELF loader
可以按路径读取 ELF header 与 segment，而不再依赖 bootloader-only exFAT helper
或嵌入式 flat image。
