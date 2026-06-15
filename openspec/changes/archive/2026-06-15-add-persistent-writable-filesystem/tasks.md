## 1. 持久卷与磁盘布局

- [x] 1.1 定义持久 `/rw` 卷的 magic、version、block size、容量边界、superblock、inode/bitmap/root directory metadata 和校验字段。
- [x] 1.2 实现并记录由构建工具生成的独立持久测试磁盘策略，确认启动盘中的 MBR、exFAT boot assets、kernel 文件和 packaged user binaries 不会被持久 `/rw` 写入覆盖。
- [x] 1.3 实现持久卷识别与拒绝路径：invalid magic、unsupported version、非法容量、metadata 不一致时确定性失败且不自动格式化。
- [x] 1.4 实现显式格式化路径，确保 superblock、位图、根目录和初始 metadata 全部写入并验证后才发布挂载。
- [x] 1.5 增加初始化阶段的挂载策略：兼容持久卷挂载为 `/rw`，失败时按配置降级为 RAM-backed `/rw` 或记录未发布持久挂载。

## 2. 块设备写与硬件边界

- [x] 2.1 审查现有块设备读/写接口，补齐持久后端需要的整扇区写、LBA 溢出校验、buffer 长度校验和只读设备拒绝写路径。
- [x] 2.2 使 ATA PIO 或当前可用写后端在轮询、写数据和 flush 超时/错误时返回确定性状态，避免无限等待或静默成功。
- [x] 2.3 明确块写只能在 port I/O 与内存管理初始化后的可阻塞普通上下文调用，并在不可阻塞上下文路径返回错误或诊断。
- [x] 2.4 复查 port I/O、设备状态轮询、flush ordering 和后续 read 契约，记录 IRQ safety、reentrancy、hardware access ordering 与 visible failure 行为。

## 3. Page/Buffer Cache 同步

- [x] 3.1 扩展缓存写回路径，使 persistent `/rw` 的 dirty data 和 metadata block 在 `fsync`、显式 sync 或淘汰时写入底层可写块设备。
- [x] 3.2 确保 `fsync` 成功前完成必要块写；写失败时保持 dirty 或 pending-write 状态，不把失败状态报告为持久提交。
- [x] 3.3 验证缓存装入/落盘仍只发生在允许阻塞、分配和同步块 I/O 的普通上下文，不从 IRQ、scheduler critical section 或 preemption-disabled 区域发起。
- [x] 3.4 检查缓存容量耗尽、无可淘汰块、脏块回写失败和缓存对象生命周期，记录分配阶段、对齐、引用计数和失败行为。

## 4. VFS 与可写后端

- [x] 4.1 把持久卷挂载接入现有 `/rw` VFS/backend 选择，同时保留 RAM-backed 非持久模式和现有用户态 syscall ABI。
- [x] 4.2 复用并验证 create/open/read/write/lseek/fsync/mkdir/unlink/restricted rename/stat/fstat/minimal directory enumeration 在持久后端上的有界语义。
- [x] 4.3 确保 owner/mode、目录可写性、路径长度、对象类型、容量、用户缓冲和 fd reference 检查在提交数据或 metadata 前完成。
- [x] 4.4 实现 clean sync 后的 mount-existing 读回路径，确认持久 inode、目录项、文件大小和数据块能跨 clean reboot 被重新解析。
- [x] 4.5 处理持久后端失败路径：容量耗尽、权限拒绝、非法路径、块 I/O 错误、metadata 提交失败和挂载失败不得污染 exFAT 或发布半成品状态。

## 5. 用户可见行为与工具

- [x] 5.1 检查现有 shell、libc wrapper 和 `/bin/*` 路径工具是否能在不新增完整 POSIX mount 工具链的情况下观察持久 `/rw` 行为。
- [x] 5.2 提供最小用户态 mkfs 工具触发独立持久测试磁盘格式化，保持入口有界，并记录它不是完整 POSIX `mkfs`/`mount`/设备管理工具链。
- [x] 5.3 更新用户可见错误展示，区分 RAM-backed current-session `/rw`、persistent clean-sync `/rw`、持久挂载失败和格式化拒绝。

## 6. 验证与静态检查

- [x] 6.1 运行 `xmake`，使用 x86_64-elf-gcc/x86_64-elf-g++ 验证默认构建；如 cross toolchain 或 xmake 不可用，记录 blocker、跳过原因和残余风险。
- [x] 6.2 为持久 FS 增加默认关闭 smoke，覆盖最小 mkfs 格式化、创建文件/目录、写入、`fsync`、第二次启动后挂载读回、metadata 查询、目录枚举和只读 exFAT 仍可读。
- [x] 6.3 使用 QEMU headless 运行持久 FS smoke，优先通过 `uv run python tools/boot_debug.py ...` 或同等 helper 在同一脚本中连续启动两次，并复用同一个独立持久测试磁盘镜像；如 `uv`、QEMU、ROM/display 或镜像路径不可用，记录 blocker。
- [x] 6.4 使用 Bochs 或 Bochs/QEMU 交叉验证 Legacy BIOS、ATA PIO、独立持久测试磁盘写回和第二次启动读回路径；如 Bochs 不可用，记录跳过原因和剩余硬件行为风险。
- [x] 6.5 运行现有 `writable_fs_smoke`、`userland_smoke` 或最窄可用替代 smoke，确认 RAM-backed `/rw` fallback 和现有 bounded userland 行为未回退。
- [x] 6.6 对修改的 C++ source/header/build 配置运行 clang 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI 配置；记录历史诊断、当前 change 新诊断和 clang 与 cross GCC 差异。
- [x] 6.7 对修改的 C++ source/header/build 配置运行 clangd 辅助诊断或记录不可用原因；修复当前 change 引入的有效错误和警告。
- [x] 6.8 若新增或修改 Python helper，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若未修改 Python 文件，记录 Python 验证不适用。

## 7. 文档与边界记录

- [x] 7.1 更新相关文档，明确 persistent `/rw` 只承诺 clean sync + clean reboot 后可见，不承诺 journaling、crash recovery、async I/O、broad storage drivers 或完整 POSIX filesystem。
- [x] 7.2 若编辑 `docs/en` 或 `docs/zh`，同步更新对应语言镜像，并保持目录结构同构。
- [x] 7.3 记录独立持久测试磁盘布局、最小用户态 mkfs 格式化方式、旧版/未知版本拒绝挂载策略、挂载失败降级、双启动验证命令、通过/跳过项、历史诊断、当前 change 诊断和残余风险。
- [x] 7.4 复查 `roadmap.md` 是否仍保持项目规划级别，不加入具体 entry point、命令、marker、源码实现细节或 archive/version 索引。
