# 内核块设备与 exFAT 读取路径

BigOS kernel block and filesystem read capability 新增内核运行期只读块设备与文件系统路径。该路径与 Legacy
BIOS bootloader 的 exFAT 辅助逻辑分离：bootloader 仍使用固定低地址缓冲区
和连续启动文件加载 `/boot/boot.bin` 与 kernel；内核运行期则在内存管理完成后
提供可复用的 bounded API。

## 范围

- 块读取使用同步只读 `BlockDevice` 契约，按完整 512-byte sector 读取到调用方
  自有缓冲区。
- 首个后端面向 Bochs 和 QEMU IDE 共用的 Legacy BIOS raw image：ATA PIO primary
  master、LBA48、同步 polling 与 bounded timeout。
- 文件系统层自动发现第一个有效 MBR exFAT 分区，校验 exFAT boot region，并
  挂载单个只读 volume。
- exFAT 支持 root path 查找、普通文件元数据、bounded read、`NoFatChain` 连续
  文件，以及 bounded FAT-chain 跟随。VFS metadata bridge 会完整初始化 atime、
  mtime、ctime 字段；只读 exFAT 后端当前返回文档化的 0 时间戳默认值，而不承诺完整
  exFAT 时间戳语义。
- 这个已归档 foundation API 仅支持普通内核上下文；不承诺 IRQ-handler-safe、DMA，
  也不由自身提供 scheduler sleep、async request lifecycle 或跨 CPU 语义；较新的
  block-layer 文档描述构建在其上的有界 interrupt-driven request 路径。

## 非目标

- 不支持写入、删除、目录修改、权限、page cache 或完整 VFS。
- 不支持 AHCI、NVMe、USB storage、DMA、热插拔或广泛 storage-device management。
- 不实现受控只读子集之外的完整 exFAT 规范。
- 不修改 MBR/DBR/extended DBR 布局、BootInfo handoff、linker 地址或现有
  first user-program smoke。

## 验证 Smoke

`xmake f --fs_smoke=y` 启用默认关闭的 runtime smoke。镜像生成器会加入
`/boot/fs_smoke.txt`，payload 为 `BIGOS_FS_SMOKE_PAYLOAD\n`，同时保持现有
`/boot/boot.bin` 与 `kernel` 布局兼容。当 `build/bin/user/init.elf` 存在时，
同一个镜像生成器也会把它打包为 `/boot/user/init.elf`，供默认关闭的 ELF 用户程序
smoke 使用。内核初始化期间，smoke 通过 ATA PIO
和 exFAT 读取该文件，并输出：

- 成功：`BIGOS_FS_EXFAT_READ_PASSED`。
- 失败：`BIGOS_FS_EXFAT_READ_FAILED code=<code>`，覆盖 mount、lookup、read
  或 payload 校验失败。

本阶段是已归档 `load-user-elf-program` change 使用的 API 基础：默认关闭的用户
ELF smoke 会按路径读取 `/boot/user/init.elf`，再由 bounded ELF64 `ET_EXEC`
loader 映射镜像，而不再依赖 bootloader-only exFAT helper 或嵌入式 flat image。
