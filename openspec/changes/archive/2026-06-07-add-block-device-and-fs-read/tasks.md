## 1. 现状复核与边界确认

- [x] 1.1 复核 `kernel/arch/x86/boot/exdbr_exfat.s`、`kernel/arch/x86/boot/boot.cc`、`tools/boot_debug.py` 和 `tools/install.py` 中的现有 exFAT/ATA 读取逻辑，记录可借鉴字段、端口序列和不能复用的 boot-stage 假设。
- [x] 1.2 确认首版磁盘布局假设：Legacy BIOS raw image、MBR exFAT 分区、ATA primary master、512-byte sector、Bochs 可观测 COM1 serial。
- [x] 1.3 确认首版 exFAT 支持范围：只读、MBR exFAT 分区自动发现、路径查找、普通文件读取、`NoFatChain` 连续文件读取和 FAT-chain 文件完整 bounded 跟随。

## 2. 块设备只读接口

- [x] 2.1 新增内核块设备公共头和类型，定义 `BlockDevice`、sector size、read status、错误码、`read_sectors(lba, count, dst, dst_len)` 契约。
- [x] 2.2 在接口层实现参数校验：空设备、空缓冲、sector count 为 0、buffer 过小、`lba + count` 溢出和字节数溢出。
- [x] 2.3 明确 API 只支持普通内核上下文，不承诺 IRQ-handler-safe、异步、sleepable 或多核并发语义。
- [x] 2.4 为块设备错误添加 bounded 文本/marker 映射，便于 smoke 输出确定性失败原因。

## 3. ATA PIO 后端

- [x] 3.1 在 `kernel/drivers` 下新增 ATA PIO 只读后端，使用现有 port I/O primitive 实现 status polling、命令发送和数据端口读取。
- [x] 3.2 实现 bounded timeout 与错误状态返回，避免设备未就绪、DRQ 不到达或错误位被置位时无限等待。
- [x] 3.3 支持 Bochs raw image 所需的 LBA 读取模式和 512-byte sector，记录 primary-master 限制和不支持 AHCI/NVMe/DMA 的原因。
- [x] 3.4 添加后端内部边界检查，确保读取数量不会写出 caller buffer，短读或设备错误不会被静默视为成功。

## 4. 基础 FS 与 exFAT 挂载

- [x] 4.1 新增最小只读 FS API，定义 mount handle、file metadata、lookup/read 结果类型和 caller-owned buffer 契约。
- [x] 4.2 实现 MBR 分区表读取与 exFAT 分区自动发现，校验 partition base/size、LBA 算术溢出和候选 boot region。
- [x] 4.3 实现 exFAT boot region 解析和校验，提取 bytes-per-sector shift、sectors-per-cluster shift、FAT offset/length、cluster heap offset、cluster count、root directory cluster 等字段。
- [x] 4.4 实现 cluster 到 LBA、文件 offset 到 sector/window 的转换，并对 cluster number、volume bounds、乘法/加法溢出做检查。
- [x] 4.5 确保 mount/read 只在普通内核上下文使用，所有临时分配失败均返回错误并清理已分配资源。

## 5. exFAT 目录与文件读取

- [x] 5.1 实现 exFAT directory entry set 解析，覆盖 file entry、stream extension、file name entries，并校验 secondary count、entry type、name length 和 data length。
- [x] 5.2 实现受限 UTF-16LE/ASCII 路径组件比较，至少支持项目测试镜像中的 `/boot/fs_smoke.txt`。
- [x] 5.3 实现绝对路径 lookup，支持 root directory 与子目录遍历；不存在路径返回 not-found，畸形目录返回 malformed。
- [x] 5.4 实现 regular file bounded read，支持从任意 file offset 读取到 caller buffer，读过 EOF 时返回实际读取字节数。
- [x] 5.5 实现 `NoFatChain` 连续文件读取。
- [x] 5.6 实现 FAT-chain 文件完整 bounded 跟随，校验 EOF、bad cluster、reserved value、cluster bounds、循环和最大遍历长度，禁止默认为连续读取。

## 6. Kernel 接入与 smoke

- [x] 6.1 在 `xmake.lua` 新增默认关闭的 `fs_smoke` 或等价开关，按开关编译/启用 FS smoke，不影响普通 boot。
- [x] 6.2 在内核初始化的安全位置接入 FS smoke，确保 memory、port I/O、serial marker 已可用，且不在 IRQ handler 中执行磁盘 I/O。
- [x] 6.3 在测试 raw image 中加入固定 exFAT 文件 `/boot/fs_smoke.txt` 和 payload，保持现有 `/boot/boot.bin` 与 `kernel` 启动布局兼容。
- [x] 6.4 smoke 成功时输出 `BIGOS_FS_EXFAT_READ_PASSED`，失败时输出 `BIGOS_FS_EXFAT_READ_FAILED code=<code>` 或进入统一 panic marker。
- [x] 6.5 若修改 Python 镜像生成工具，使用 `uv run ...` 路径执行相关工具/测试；若 `uv` 不可用，记录阻塞原因与剩余风险。

## 7. 文档与规格同步

- [x] 7.1 更新相关 docs，说明 kernel runtime block/FS 与 bootloader exFAT 读取路径的区别、支持范围和非目标。
- [x] 7.2 若更新 `docs/en`，同步更新对应 `docs/zh` 路径，保持双语目录结构一致。
- [x] 7.3 在路线图或验证说明中记录阶段 7 对阶段 8 `load-user-elf-program` 的 API 前置关系。

## 8. 验证与回归

- [x] 8.1 运行 `openspec validate add-block-device-and-fs-read --strict`，修复 proposal/design/spec/tasks 格式或 requirement/scenario 问题。
- [x] 8.2 运行默认配置 `xmake`，确认普通 boot 构建不依赖 FS smoke 或运行期读盘。
- [x] 8.3 运行 `xmake f --fs_smoke=y` 后执行 `xmake`，确认新增 C++/header/build 配置可通过 cross GCC freestanding 构建。
- [x] 8.4 针对 C++ 改动执行尽可能贴近 freestanding x86_64 配置的 clang/clangd 辅助静态检查；若工具或 flag 不可用，记录不可运行原因、历史诊断和剩余风险。
- [x] 8.5 在 Bochs 可用时运行 `xmake run bochs-sdl2` 或 `xmake run bochs`，确认 serial 输出 `BIGOS_FS_EXFAT_READ_PASSED`；若 Bochs/ROM/toolchain 不可用，记录 blocker 与 bootability 风险。
- [x] 8.6 复核低层风险：port I/O 顺序、polling timeout、MBR 分区 bounds check、FAT 链循环检测、buffer 边界、目录项 bounds check、分配失败清理、普通 boot 不变、boot fixed addresses/BootInfo/linker 地址未被修改。
- [x] 8.7 记录验证结果，区分已通过检查、无法运行检查及原因、历史诊断、当前 change 新增问题和后续风险。

## 验证记录

- 已通过：`openspec validate add-block-device-and-fs-read --strict`。
- 已通过：默认 `xmake`，确认普通 build 不依赖 `BIGOS_FS_SMOKE` 运行期读盘。
- 已通过：`xmake f --fs_smoke=y && xmake`，确认 smoke 配置可由 freestanding cross GCC 构建。
- 已通过：`uv run python -m py_compile tools/boot_debug.py` 与 `uv run python tools/boot_debug.py validate-image --image build/test/os.raw`。
- 已通过：VS Code diagnostics 无新增诊断。
- 已复位：验证后执行 `xmake f --fs_smoke=n`，保持本地默认配置关闭 FS smoke。
- Bochs runtime 未通过：`uv run python tools/boot_debug.py run --serial-log build/test/serial.log --expect-serial-marker BIGOS_FS_EXFAT_READ_PASSED --smoke-timeout 30` 超时，未生成 COM1 serial log；同环境下 `fs_smoke` 关闭并等待既有 `BigOS kernel reached` marker 也超时，`bochs.log` 仅显示 BIOS 启动后被测试脚本 SIGTERM，故记录为本地 Bochs/serial 可观测性 blocker，仍保留 bootability 风险。
