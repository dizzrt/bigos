## Validation

### 已通过检查

- `xmake`
  - 默认关闭配置构建通过，确认默认 boot/storage/filesystem/userland baseline 不依赖 virtio-net。
- `xmake f --virtio_net_smoke=y && xmake`
  - virtio-net 默认关闭 smoke 配置构建通过。
- `xmake f --virtio_blk_smoke=y && xmake`
  - virtio common helper 提取后，virtio-blk smoke 配置构建通过。
- `uv run python -m tools.bigosdev smoke matrix --case default-init --case modern-virtio-blk --output logs/virtio-net-regression-smoke.md --serial-log-dir logs/virtio-net-regression --image-dir build/test/virtio-net-regression --keep-going --record-bochs`
  - `default-init` 通过，观测 `BIGOS_USER_EXEC`。
  - `modern-virtio-blk` 通过，观测 `BIGOS_VIRTIO_BLK_PUBLISHED` 与 `BIGOS_VIRTIO_BLK_PASSED`。
  - 产物：`logs/virtio-net-regression-smoke.md`。
- `uv run pytest tests/test_modern_block_storage_driver_source.py tests/test_virtio_net_driver_source.py`
  - 10 个 source-level 测试通过，覆盖 virtio common helper、virtio-blk 复用、virtio-net 网络设备边界、RX/TX queue、MSI-X IRQ-safe 约束、默认关闭 smoke 和 TAP helper。
- `uv run ruff check tools/virtio_net_tap.py tools/bigosdev/core.py tests/test_virtio_net_driver_source.py tests/test_modern_block_storage_driver_source.py`
  - 通过。
- `uv run ruff format --check tools/virtio_net_tap.py tools/bigosdev/core.py tests/test_virtio_net_driver_source.py tests/test_modern_block_storage_driver_source.py`
  - 通过。
- `uv run pyright`
  - 通过。
- `clang++ -std=c++17 -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/drivers/virtio/pci.cc kernel/drivers/net/virtio_net.cc kernel/drivers/block/virtio_blk.cc`
  - 通过。
- `uv run python tools/virtio_net_tap.py --dry-run prepare`
  - 当前 Darwin host 上正确报告 `BIGOS_VIRTIO_NET_TAP_SKIPPED unsupported host platform: Darwin`。

### 无法完成的运行时覆盖

- `uv run python -m tools.bigosdev smoke matrix --case modern-virtio-net --output logs/virtio-net-runtime-smoke.md --serial-log-dir logs/virtio-net-smoke --image-dir build/test/virtio-net-smoke --keep-going --record-bochs`
  - 状态：`blocked`。
  - 原因：当前 host 是 Darwin，缺少 Linux TAP `/dev/net/tun` 支持；工具 preflight 阻止启动 QEMU 并记录 skip。
  - 产物：`logs/virtio-net-runtime-smoke.md`。
  - 剩余风险：未在本机观测 `BIGOS_VIRTIO_NET_PASSED`，也未完成 tap-backed 可控 RX frame 注入闭环。
- Bochs virtio-net runtime 未执行。
  - 原因：本变更的 virtio-net runtime 目标是 QEMU modern virtio-net + MSI-X + TAP；Bochs 仅记录为默认启动/中断边界可用后端。
  - 替代覆盖：QEMU `default-init` 通过，virtio-blk QEMU MSI-X 回归通过，Bochs 可用性已记录在 runtime smoke 产物中。

### 历史或非本变更诊断

- `uv run pytest`
  - 结果：327 passed, 18 failed。
  - 失败集中在既有 source-level 字符串断言、已归档/缺失的旧 OpenSpec change 路径、VM/proc/syscall 历史实现字符串漂移，以及归档 validation 中旧 `src/kernel` 文本。
  - 本变更相关 targeted tests 已单独通过；未把这些历史失败作为 virtio-net 当前变更新增失败处理。
- `clangd --check=kernel/drivers/net/virtio_net.cc --compile-commands-dir=.`
  - 结果：Apple clangd 21 返回 3，输出为 `tweak: ExtractFunction ==> FAIL: Cannot extract break/continue without corresponding loop/switch statement.`，未见新增 C++ 语义诊断。
- `clangd --check=kernel/drivers/virtio/pci.cc --compile-commands-dir=.`
  - 结果：Apple clangd 21 返回 3，同类 ExtractFunction tweak 自测失败，未见新增 C++ 语义诊断。
- `clangd --check=kernel/drivers/block/virtio_blk.cc --compile-commands-dir=.`
  - 结果：Apple clangd 21 返回 3，同类 ExtractFunction tweak 自测失败，未见新增 C++ 语义诊断。

### 残余风险

- virtio-net 首版仍没有协议栈、socket ABI、用户 fd 类型或用户态网络工具；当前接口只发布内核内部 frame-level 网络设备。
- 当前环境未完成 Linux TAP-backed runtime RX/TX 闭环；需要在具备 `/dev/net/tun`、`ip tuntap`、QEMU `virtio-net-pci` 和足够权限的 host 上执行 `tools/virtio_net_tap.py prepare` 后重跑 `modern-virtio-net`。
- virtio-blk 为保持现有 QEMU 回归，继续接受 transitional PCI device id 上暴露的 modern capability；virtio-net 探测保持 modern device id only，不回退 legacy/transitional IO-port transport。
