## Context

当前 BigOS 的 exFAT 读取能力存在于 Legacy BIOS 启动链路：

- `kernel/arch/x86/boot/exdbr_exfat.s` 在 real/protected mode 阶段查找 `/boot/boot.bin`。
- `kernel/arch/x86/boot/boot.cc` 使用 ATA PIO 读取连续文件 `kernel` 并加载 ELF64 内核。
- 这些路径依赖固定低地址缓冲区、固定目录层级、固定磁盘后端和启动期 ABI，不适合作为内核 runtime API 直接复用。

kernel block and filesystem read capability 的目标是在 kernel 初始化后提供只读块设备与基础 FS 能力。该能力运行在单核、非 IRQ 上下文，依赖已有 direct map、kernel vmem、buddy/slab、COM1 marker 和 xmake/Bochs smoke 机制。它不改变 MBR/DBR/extended DBR/BootInfo handoff，也不改变现有 first user program smoke。

目标数据流：

```text
kernel init / fs smoke
  -> driver::block::AtaPioBlockDevice::read_sectors(lba, count, buffer)
  -> bigos::fs::find_exfat_partition(block_device)
  -> bigos::fs::exfat::mount(block_device, discovered_partition_lba)
  -> bigos::fs::read_file("/boot/fs_smoke.txt", dst, len, offset)
  -> COM1 marker BIGOS_FS_EXFAT_READ_PASSED / FAILED
```

## Goals / Non-Goals

**Goals:**

- 定义内核态只读块设备接口，使用 LBA + sector count + caller buffer 作为基础契约。
- 实现首个 ATA PIO primary-master 后端，支持 Bochs raw disk image 上的同步扇区读取。
- 定义最小只读 FS 接口，允许从 MBR 分区表自动发现 exFAT 分区、挂载 exFAT volume 并按绝对路径读取普通文件。
- 实现 exFAT 只读解析：boot region 参数校验、cluster 到 LBA 转换、root/subdirectory 遍历、目录项集合解析、文件名匹配、文件内容 bounded read。
- 允许读取连续文件，并完整跟随 FAT 链文件；所有链表遍历必须有 bounds check 和循环检测。
- 提供默认关闭的 runtime smoke，读取固定 exFAT 测试文件 `/boot/fs_smoke.txt` 验证 kernel runtime 读取闭环。

**Non-Goals:**

- 不实现写入、删除、目录创建、目录修改、权限、锁、缓存一致性、page cache 或完整 VFS。
- 不实现 AHCI/NVMe/USB/SCSI、DMA、异步 I/O、中断驱动 I/O 或多设备枚举。
- 不实现完整 exFAT 规范全集，包括完整 upcase table、本地化大小写折叠、时间戳、TexFAT、bitmap 修改或损坏卷修复。
- 不实现从 FS 加载用户 ELF；user ELF loader capability 将在本阶段 API 之上定义用户程序 ELF loader。
- 不改变 bootloader 磁盘布局、固定低地址、BootInfo ABI、linker 地址或页表 self-mapping 地址。

## Decisions

### Decision: 块设备先使用同步只读接口

接口形态：

```text
read_sectors(lba: u64, sector_count: u16/u32, dst: void*, dst_len: usize) -> BlockStatus
```

理由：

- 当前内核没有阻塞语义、sleep queue、异步通知或 IO scheduler，同步读取最符合现有单核 cooperative runtime。
- caller-owned buffer 能避免在 driver 内隐藏分配，也让 FS 层明确控制临时缓冲生命周期。
- sector count 与 buffer size 的 bounded 校验能防止 ATA PIO 路径写出 caller buffer。

备选方案：

- 直接暴露 `read(offset, bytes)`：更方便 FS 使用，但会把 sector 对齐和 bounce buffer 策略隐含到 driver 层，早期不利于诊断。
- 一开始实现 VFS/block cache：抽象更完整，但会引入缓存生命周期、脏页和一致性问题，超出kernel block and filesystem read capability 目标。

### Decision: ATA PIO 后端独立于 bootloader 代码重建

新后端可以借鉴 `boot.cc` 的端口序列和超时模型，但应放在 kernel driver 目录，使用 runtime 可用的 port I/O、panic/marker 和内存 API。

理由：

- bootloader 代码运行阶段、地址假设、缓冲区和错误处理均不同于内核。
- 内核 driver 需要清晰声明 primary master、LBA48、512-byte sector、polling timeout 等约束。
- 保持 bootloader 路径不变可降低 bootability 回归风险。

备选方案：

- 直接共享 `boot.cc` 代码：会把启动期固定地址和 freestanding runtime 初始化顺序混入内核。
- 先做 AHCI：更接近现代硬件，但需要 PCI/MMIO/DMA 基础，当前项目尚未具备。

### Decision: FS 层保持单挂载、只读、路径读取 API

初始 API 面向后续 ELF user loader：

```text
find_exfat_partition(BlockDevice&) -> Partition
mount_exfat(BlockDevice&, Partition) -> Mount
read_file(Mount&, absolute_path, offset, dst, len) -> FsResult
```

理由：

- user ELF loader capability 需要按路径读取 ELF 文件内容，但不需要目录修改、文件描述符表或进程级 VFS。
- 单挂载对象能避免全局 namespace、mount table 和锁语义。
- `offset + len` bounded read 可以直接服务 ELF header/program header/segment 分段读取。
- 自动发现 MBR exFAT 分区能避免把 bootloader 的固定分区 LBA 假设泄漏给 kernel FS caller。

备选方案：

- 完整 VFS + inode/file descriptor：后续可演进，但kernel block and filesystem read capability 会引入过多长期 API。
- 仅实现 exFAT 私有函数无通用 FS 接口：实现更快，但会让user ELF loader capability 与 exFAT 绑定过深。
- 由调用方传入 partition base LBA：实现更简单，但会让 smoke 与后续 loader 依赖外部硬编码磁盘布局。

### Decision: exFAT 实现选择“可验证子集”

支持范围：

- 校验 volume boot record 的基本字段和 `EXFAT` 文件系统名。
- 解析 MBR 分区表并自动选择支持的 exFAT 分区作为 mount 输入。
- 使用 bytes per sector shift、sectors per cluster shift、FAT offset/length、cluster heap offset、root directory cluster 等字段。
- 解析 file directory entry、stream extension entry、file name entries 的目录项集合。
- 支持 ASCII/UTF-16LE 可表示路径组件匹配，至少覆盖测试镜像中的 `/boot/fs_smoke.txt`。
- 支持 `NoFatChain` 连续文件读取，并完整跟随 FAT 链文件；FAT 链遍历必须校验 cluster bounds、EOF marker、bad cluster、保留值和循环。

理由：

- 这覆盖当前镜像工具和 bootloader 的实际布局，同时把路径查找与文件读取变成 kernel API。
- 完整 FAT 链跟随使kernel block and filesystem read capability 的“支持 exFAT”不依赖文件连续布局，也能为user ELF loader capability 的 ELF loader 降低镜像布局约束。
- 自动 MBR 发现让 kernel runtime 自己定位 exFAT volume，避免后续 API 传播 partition base LBA 常量。
- 明确拒绝其他未支持格式比 silent misread 更安全。

备选方案：

- 实现完整 exFAT：需要 upcase table、allocation bitmap、checksum、时间戳、目录扩展等大量细节，不适合kernel block and filesystem read capability。
- 改用自定义简单 FS：实现更低成本，但用户明确要求支持 exFAT，且现有工具链已经能生成 exFAT 镜像。

### Decision: 初始化与 smoke 默认关闭

FS smoke 使用 `xmake f --fs_smoke=y` 或等价开关启用，普通 boot 不主动挂载或读盘。smoke 成功输出 `BIGOS_FS_EXFAT_READ_PASSED`，失败输出 `BIGOS_FS_EXFAT_READ_FAILED code=<code>`。

理由：

- 磁盘 I/O 失败与本地 Bochs/ROM/镜像布局强相关，默认关闭能保护普通 boot。
- 稳定 COM1 marker 便于 `tools/boot_debug.py` 做 serial-marker 判定。

备选方案：

- 普通 boot 默认挂载 root FS：更接近 OS 行为，但会把磁盘后端稳定性变成所有启动路径的硬依赖。

## Risks / Trade-offs

- [Risk] ATA PIO primary-master 假设只覆盖当前 Bochs raw image。-> Mitigation: 在 spec 与代码注释中明确后端能力，保留 BlockDevice 接口以便后续 AHCI/NVMe 替换。
- [Risk] exFAT 子集可能读取不了真实世界 volume。-> Mitigation: 只承诺项目生成镜像和受控只读子集；未知/不支持目录项返回错误，不做猜测读取。
- [Risk] FAT 链跟随可能遇到循环、坏 cluster 或越界 cluster。-> Mitigation: 每一步校验 cluster 范围、FAT entry 值、最大遍历步数和累计读取长度，异常时返回明确错误。
- [Risk] MBR 自动发现可能遇到多个 exFAT-like 分区。-> Mitigation: 首版选择第一个类型/boot region 校验均通过的 exFAT 分区，并在诊断中记录选择结果。
- [Risk] 同步 polling I/O 可能长时间占用 CPU。-> Mitigation: 仅在非 IRQ 上下文和 smoke/加载路径使用，加入 bounded timeout 与错误码。
- [Risk] 临时缓冲分配失败会导致挂载或读取失败。-> Mitigation: API 返回显式错误，不在 IRQ handler 中调用，不在失败路径泄漏分配。
- [Risk] 路径解析和 UTF-16LE 文件名比较容易出现越界。-> Mitigation: 所有目录项集合使用固定 entry count 与 bounds check，路径组件长度设上限。
- [Risk] 修改镜像生成工具可能影响现有 boot smoke。-> Mitigation: 保持已有 `/boot/boot.bin` 与 `kernel` 布局不变，只追加 FS smoke 测试文件。

## Migration Plan

- 新增代码默认不接入普通 boot；先通过 `fs_smoke` 编译开关验证。
- 若 smoke 失败，可关闭 `fs_smoke` 回退到现有启动行为；bootloader 与用户程序 smoke 不依赖本 change。
- 归档前同步kernel block and filesystem read capability 规格、runtime marker、测试镜像说明和后续user ELF loader capability 依赖说明。

## Resolved Decisions

- 首版必须完整跟随 FAT 链；链式文件不能返回 `Unsupported` 作为常规能力边界。
- `fs_smoke` 测试文件路径固定采用 `/boot/fs_smoke.txt`。
- 首版从 MBR 分区表自动发现 exFAT 分区，不要求调用方传入 partition base LBA。
