# 验证记录

## 已通过

- `xmake`：默认配置构建通过，使用当前可用 xmake 与 `x86_64-elf-*` 工具链。
- `xmake f --persistent_writable_fs_smoke=y && xmake`：默认关闭的 persistent `/rw` smoke 编译路径通过。
- persistent `/rw` smoke 覆盖面已扩展：最小格式化、创建文件/目录、写入、`fsync`、缓存淘汰后读回、mount-existing 读回分支、metadata/stat、目录枚举、restricted rename、unlink 和只读 exFAT asset 读回均有默认关闭 smoke 代码路径。
- `uv run python tools/boot_debug.py run --skip-build --emulator qemu --display none --persistent-image build/test/persistent-rw.raw --serial-log build/test/persistent-write.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED --smoke-timeout 45`：QEMU headless 使用独立 `isa-ide` persistent test controller 首次启动通过，串口观察到 `BigOS kernel reached` 与 `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`。
- `uv run python tools/boot_debug.py run --skip-build --emulator qemu --display none --persistent-image build/test/persistent-rw.raw --serial-log build/test/persistent-verify.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED --smoke-timeout 45`：复用同一 QEMU persistent image 第二次启动通过，确认 mount-existing 后持久 inode、目录项、文件大小和数据块可读回。
- `uv run python tools/boot_debug.py run --skip-build --emulator bochs --display none --persistent-image build/test/persistent-rw-bochs.raw --serial-log build/test/persistent-bochs-write.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED --smoke-timeout 45`：Bochs headless 使用相同独立 persistent test controller 首次启动通过，覆盖 Legacy BIOS、ATA PIO 与独立持久测试磁盘写回路径。
- `uv run python tools/boot_debug.py run --skip-build --emulator bochs --display none --persistent-image build/test/persistent-rw-bochs.raw --serial-log build/test/persistent-bochs-verify.serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED --smoke-timeout 45`：复用同一 Bochs persistent image 第二次启动通过，完成 Bochs/QEMU 交叉验证。
- `xmake f --persistent_writable_fs_smoke=n --persistent_writable_fs=n --writable_fs_smoke=y && xmake`：RAM-backed `/rw` fallback smoke 编译路径通过。
- `uv run python tools/boot_debug.py run --skip-build --emulator qemu --display none --serial-log build/test/writable-fs.serial.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED --smoke-timeout 30`：现有 RAM-backed `/rw` smoke 在 QEMU headless 通过。
- `xmake build user-init-elf`：包含 `/bin/mkfs_bigfs` 的用户态打包目标构建通过。
- `uv run python tools/boot_debug.py run --skip-build --no-launch --emulator qemu --display none --persistent-image build/test/persistent-rw.raw`：可生成 Legacy BIOS 启动镜像，并创建/记录独立 persistent `/rw` 测试磁盘路径。
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/kernel.cc kernel/core/syscall/syscall.cc kernel/drivers/block/ata_pio.cc`：修改的关键 C++ source/header 辅助语法检查通过。
- `uv run python -m py_compile tools/boot_debug.py`：Python helper 语法检查通过。
- `uv run ruff check tools/boot_debug.py`：Python helper lint 通过。
- `uv run ruff format --check tools/boot_debug.py`：Python helper format 检查通过。
- `uv run pyright tools/boot_debug.py`：Python helper 类型检查通过；环境提示存在新版 pyright，但当前检查为 0 errors。
- `uv run pytest tests/test_syscall_entry_source.py tests/test_writable_fs_page_cache_pipe_source.py`：受影响 syscall mirror 与 writable FS/page-cache source-contract 测试通过。
- clangd/IDE diagnostics：`GetDiagnostics` 未报告当前编辑文件的新诊断。

## 阻塞或跳过

- 历史 blocker 已解除：默认 PIIX IDE 第二盘拓扑会干扰 Legacy BIOS/boot 或与默认 `ide-cd` 占位冲突；当前 QEMU 与 Bochs 均改用 helper-owned ISA IDE controller（`iobase=0x168`、`iobase2=0x36e`、`irq=10`）承载独立 persistent test disk。
- 历史 Bochs 第二启动曾因 persistent image lock 和默认 init 与 smoke 并发访问 ATA PIO 造成不稳定；当前 helper 使用 `bochs -unlock`，persistent smoke 配置下不并发启动默认 PID-1 init，smoke 内仍检查只读 exFAT asset。
- clang 辅助检查第一次使用宿主默认 target 时失败：arm64-apple-darwin clang 不接受 x86 `-mno-sse/-mno-sse2/-mno-mmx`；改用 `--target=x86_64-elf` 后通过。
- 完整 Python 测试集未声明通过：`uv run pytest tests/test_syscall_entry_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_fd_vfs_shell_source.py` 中 `test_fd_vfs_shell_source.py` 存在历史 source-contract/缺失 OpenSpec 文档失败；本 change 直接影响的 syscall 与 writable FS source-contract 测试已单独通过。

## 残余风险

- 当前 persistent `/rw` runtime smoke 已在 QEMU 与 Bochs 上通过双启动验证；残余风险主要是该验证使用固定 ISA IDE PIO test controller，不代表 AHCI/SATA/NVMe、UEFI、virtio 或广泛真实硬件存储拓扑。
- persistent `/rw` 只承诺 clean `fsync`/write-back 后 clean reboot 可见；不承诺 journaling、crash recovery、power-loss safety、async I/O、广泛存储驱动或完整 POSIX filesystem。
- `/bin/mkfs_bigfs` 仅触发 BigOS 专用的有界 persistent test disk 格式化，不是通用 POSIX `mkfs`、`mount` 或设备管理工具。
- `roadmap.md` 已复查，仍保持项目规划级别；未加入具体 entry point、命令、marker、源码实现细节或 archive/version 索引。
