## 验证记录

日期：2026-06-24

## 已通过

- `xmake f --pci_msix_smoke=y && xmake`
  - 启用默认关闭 MSI-X smoke 的内核构建通过。
- `xmake f --pci_msix_smoke=n && xmake`
  - 默认关闭 MSI-X smoke 的内核构建通过。
- `clang++ --target=x86_64-unknown-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_AP_STARTUP_PERCPU_TIMERS -DBIGOS_PCI_MSIX_SMOKE -DBIGOS_USER_PROCESS -fsyntax-only kernel/drivers/pci/msix.cc`
  - 新增 MSI-X 模块的 freestanding C++17 语法检查通过。
- `uv run pytest tests/test_pci_msix_interrupt_delivery_source.py tests/test_pci_config_access_and_vector_alloc_source.py tests/test_apic_default_interrupt_delivery_source.py tests/test_framebuffer_boot_handoff_source.py`
  - 17 项源级检查通过。
- `xmake f --pci_msix_smoke=y && uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --serial-log logs/pci-msix-smoke-legacy.log --expect-serial-marker BIGOS_PCI_MSIX_PASSED --smoke-timeout 30`
  - Legacy BIOS/QEMU headless 下观察到 `BIGOS_PCI_MSIX_PASSED`。
- `xmake f --pci_msix_smoke=n && uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --serial-log logs/default-legacy-regression.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 30`
  - 默认关闭 MSI-X smoke 时，Legacy BIOS/QEMU headless baseline 到达 `BIGOS_USER_EXEC`。
- `xmake f --pci_msix_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log logs/pci-msix-smoke.log --expect-serial-marker BIGOS_PCI_MSIX_PASSED --smoke-timeout 30`
  - helper 默认 UEFI/QEMU headless 路径同样观察到 `BIGOS_PCI_MSIX_PASSED`，作为补充覆盖。

## 诊断与跳过项

- `uv run pytest` 全量源级测试当前为 304 passed / 19 failed。失败项集中在历史字符串断言或缺失的已归档/已移除 OpenSpec 活跃目录，例如 `flush_kernel_tlb_page(...)` 旧命名、`default_external_irq_handler` 旧符号、`add-metadata-consistency` 活跃变更目录缺失、以及归档 validation 中的旧 `src/kernel` 文本。本次 MSI-X 相关 targeted tests、xmake、clang 与 QEMU smoke 均通过；全量失败按历史诊断记录，未作为本次 MSI-X 阻塞项处理。
- `clangd --check=kernel/drivers/pci/msix.cc --compile-commands-dir=.` 可构建 AST，但退出码为 3；记录的错误为 clangd check-mode 的 `ExtractFunction` tweak 自检失败（`Cannot extract break/continue without corresponding loop/switch statement`），不是源码诊断。`clang++ -fsyntax-only` 与 xmake 交叉工具链构建均通过，因此按工具限制记录为非阻塞残余风险。
- 当前 smoke 在无真实 MSI-X PCI function 时会记录 `BIGOS_PCI_MSIX_CAPABILITY_SKIPPED unsupported`，并使用受控 LAPIC fixed IPI producer 验证“编程向量 == 实际 handler”、mask 抑制与 unmask 恢复。真实设备 MSI-X 时序仍留待后续具体设备驱动变更验证。
- Bochs MSI-X 运行时投递未验证；本次不声明 Bochs MSI-X runtime 成功。

## 残余风险

- 首版目标 CPU 固定为 BSP，不覆盖 IRQ affinity、SMP 目标重排或 x2APIC 大 APIC ID 的完整 MSI-X destination 扩展。
- MSI-X table/PBA 映射与配置路径已实现并源级覆盖，但当前 runtime smoke 没有绑定真实支持 MSI-X 的 PCI 设备 BAR；真实设备 BAR 布局与设备 pending 行为需在后续现代设备驱动中继续验证。
