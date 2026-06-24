## Validation

- `xmake f --virtio_blk_smoke=y && xmake`
  - 启用默认关闭 virtio-blk smoke 的内核构建通过。
- `xmake f --virtio_blk_smoke=n && xmake`
  - 默认关闭 virtio-blk smoke 的内核构建通过；驱动编译进内核但不执行验证 probe。
- `uv run pytest tests/test_modern_block_storage_driver_source.py tests/test_pci_msix_interrupt_delivery_source.py tests/test_block_io_request_layer_source.py tests/test_device_driver_framework_source.py`
  - 21 passed；覆盖 modern virtio PCI capability 解析、split virtqueue 物理 DMA、MSI-X completion IRQ-safe、块请求 token/generation、设备框架内部角色与默认关闭验证边界。
- `uv run pytest tests/test_modern_block_storage_driver_source.py tests/test_boot_debug.py`
  - 44 passed；覆盖 smoke 矩阵与 QEMU virtio-blk 参数记录。
- `clang++ --target=x86_64-unknown-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_AP_STARTUP_PERCPU_TIMERS -DBIGOS_VIRTIO_BLK_SMOKE -DBIGOS_USER_PROCESS -fsyntax-only kernel/drivers/block/virtio_blk.cc`
  - 新增 virtio-blk 模块的 freestanding C++17 语法检查通过。
- `clangd --check=kernel/drivers/block/virtio_blk.cc --compile-commands-dir=.`
  - 可构建 AST，但退出码为 3；记录的错误为 Apple clangd check-mode 的 `ExtractFunction` tweak 自检失败（`Cannot extract break/continue without corresponding loop/switch statement`），未报告源码语法/语义诊断。按工具限制记录为非阻塞残余风险。
- `dd if=/dev/zero of=build/test/virtio-blk.raw bs=1m count=16`
  - 准备独立 virtio-blk raw 验证盘，避免写后读 smoke 修改默认 ATA boot image。
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --skip-build --serial-log logs/virtio-blk-smoke-uefi.log --expect-serial-marker BIGOS_VIRTIO_BLK_PASSED --smoke-timeout 30 --qemu-extra "-drive if=none,id=virtioblk,file=build/test/virtio-blk.raw,format=raw -device virtio-blk-pci,drive=virtioblk,disable-modern=off"`
  - UEFI/QEMU headless 下观察到 `BIGOS_VIRTIO_BLK_PUBLISHED` 与 `BIGOS_VIRTIO_BLK_PASSED`，覆盖设备发布、写后读往返、MSI-X used completion 唤醒同步等待者，以及越界 LBA 确定性失败路径。
- `uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --skip-build --serial-log logs/default-legacy-after-virtio-blk.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 30`
  - 默认关闭 virtio-blk smoke 时，Legacy BIOS/QEMU headless baseline 到达 `BIGOS_USER_EXEC`，默认 ATA/exFAT/userland 路径不依赖 virtio-blk。

## Skipped / Residual Risk

- Legacy BIOS/QEMU headless 加挂额外 virtio-blk PCI 设备时，本机 SeaBIOS/QEMU 组合在 30 秒内没有串口输出；未声明 Legacy BIOS + virtio-blk runtime smoke 成功。已通过默认关闭 Legacy baseline 回归确认不影响默认启动，并通过 UEFI/QEMU 覆盖真实 virtio-blk + MSI-X I/O 闭环。
- Bochs virtio-blk/MSI-X runtime 未验证；Bochs 当前仍只作为默认 legacy/ATA 路径的候选验证环境，不声明 Bochs modern virtio-blk 成功。
- 首版扫描边界仍为 bus 0 的 QEMU 目标设备，支持 modern non-transitional PCI ID `0x1042`，也接受 transitional PCI ID `0x1001` 但只使用 modern virtio PCI capability，不走 legacy IO port transport。
