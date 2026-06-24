## 1. 设计确认与向量区间规划

- [x] 1.1 梳理现有固定向量集合（异常 0x00-0x1f、PIC remap 0x20-0x2f、syscall 0x80、LAPIC timer 0xef、sched nudge 0xee、TLB shootdown 0xed）并确定不冲突的动态向量区间
- [x] 1.2 在公共头集中登记动态向量区间常量与注释，确认与 `VectorOwner` 分类一致
- [x] 1.3 确认 PCI 配置访问与向量分配的上下文边界（仅普通可阻塞内核上下文）

## 2. PCI 配置空间访问实现

- [x] 2.1 实现基于 0xCF8/0xCFC 的 32 位对齐 dword 读，并派生 8/16 位读（`kernel/drivers` 下新增 PCI 访问模块）
- [x] 2.2 实现 vendor/device 探测与“无设备”确定性判定
- [x] 2.3 实现 capability list 遍历，含末尾终止、越界/循环防御与有界步数
- [x] 2.4 实现 BAR 描述读取：IO/内存类型、32/64 位宽度、可预取标志、size 探测后恢复原值
- [x] 2.5 添加最小 PCI 公共头，仅暴露消费方必需接口，端口常量留在实现文件

## 3. 内核中断向量分配实现

- [x] 3.1 实现受限区间的向量分配/释放，复用现有 ISR 注册并标注 `VectorOwner::Lapic`
- [x] 3.2 实现耗尽返回确定性错误、重复释放确定性拒绝/诊断
- [x] 3.3 确认分配向量触发走现有 `InterruptFrame` 分发并经 LAPIC EOI 唯一发送，不触碰 PIC EOI

## 4. 默认关闭验证入口

- [x] 4.1 新增默认关闭构建开关，串联 PCI 探测/遍历/BAR 读取与向量分配/释放/重复释放/耗尽的 smoke 入口，按既有 COM1/VGA marker 风格输出 pass/fail
- [x] 4.2 添加源级检查：向量区间不与固定向量冲突、配置访问与分配仅在可阻塞上下文、EOI 所有权唯一、IRQ 路径无阻塞/分配

## 5. 构建与静态检查

- [x] 5.1 运行 `xmake`（x86_64-elf-gcc/g++）确认默认配置与启用验证开关均可编译
- [x] 5.2 运行 clang 与 clangd 辅助静态检查（freestanding C++17、x86_64 target、项目 include、无 hosted/异常/RTTI），修复本次引入的 error 并确认/修复有效新增 warning；若工具不可用则记录原因与残余风险
- [x] 5.3 区分历史诊断、本次变更诊断与工具链/freestanding 误报

## 6. 仿真器验证与记录

- [x] 6.1 在 QEMU（`--display none`）下运行启用验证开关的 smoke，观察 PCI 探测/遍历/BAR 与向量生命周期 marker，日志写入 `logs/`
- [x] 6.2 默认启动回归：确认默认关闭验证时 boot/shell/`/rw`/userland baseline 行为不变
- [x] 6.3 记录验证：通过项、因 QEMU/Bochs/工具链不可用而跳过的项与残余风险分别记录，不对跳过环境声明运行时成功

## 7. 中断/驱动安全审查

- [x] 7.1 审查中断安全与重入：分配/释放不在 IRQ 上下文，分发与 EOI 所有权未被破坏
- [x] 7.2 审查硬件访问顺序：BAR size 探测写后恢复、配置访问对齐、可见失败路径确定性

## 验证记录

- 通过：`uv run pytest tests/test_pci_config_access_and_vector_alloc_source.py`。
- 通过：`xmake`（默认关闭 `pci_config_vector_smoke`）。
- 通过：`xmake f --pci_config_vector_smoke=y && xmake`。
- 通过：`clang++ -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -target x86_64-elf -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_USER_PROCESS -DBIGOS_PCI_CONFIG_VECTOR_SMOKE -fsyntax-only kernel/drivers/pci/config.cc kernel/core/bigos/io.cc kernel/core/irq/interrupt.cc kernel/core/kernel.cc`。
- 通过：`clangd --check=kernel/drivers/pci/config.cc --compile-commands-dir=.`，0 errors。
- 通过：`uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --serial-log logs/pci_config_vector_smoke_legacy_serial.log --expect-serial-marker BIGOS_PCI_CONFIG_VECTOR_PASSED`。
- 通过：`uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --serial-log logs/default_legacy_after_pci_vector_serial.log --expect-serial-marker BIGOS_USER_EXEC`。
- 额外通过：未显式指定 `--boot-mode` 的 QEMU helper 默认 UEFI backend smoke，观测 `BIGOS_PCI_CONFIG_VECTOR_PASSED`，日志 `logs/pci_config_vector_smoke_serial.log`。
- 历史诊断：扩展运行 `tests/test_apic_default_interrupt_delivery_source.py` 时，既有测试 `test_apic_default_hard_irq_paths_remain_nonblocking` 的字符串切片跨过 `primary_ide` handler，命中既有 `driver::block::ata_pio_primary_irq()` 后失败；该失败与本次新增 PCI/向量代码无关。
- 未运行：Bochs cross-validation。本变更要求的 QEMU headless smoke 与默认启动回归已覆盖；不声明 Bochs 运行时成功。
