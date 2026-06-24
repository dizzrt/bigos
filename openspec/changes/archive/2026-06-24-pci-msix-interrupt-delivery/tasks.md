## 1. 前置接口梳理与设计确认

- [x] 1.1 梳理 `pci-config-access` 的 capability 遍历与 BAR 描述接口、`kernel-irq-vector-allocation` 的向量分配/注册接口，确认 MSI-X 所需输入输出
- [x] 1.2 确认 MSI-X table/PBA 的 MMIO 映射方式与现有 LAPIC/IOAPIC 映射一致（不可缓存、有界）
- [x] 1.3 确认目标 CPU 首版固定为 BSP，并与现有默认中断投递目标选择一致

## 2. MSI-X capability 解析

- [x] 2.1 实现 MSI-X capability 定位与解析：table size、table BAR 索引/偏移、PBA BAR 索引/偏移
- [x] 2.2 实现 function-level enable 与 function mask 位的读写
- [x] 2.3 实现“设备不支持 MSI-X”的确定性返回与不可阻塞上下文拒绝

## 3. table/PBA 映射与条目编程

- [x] 3.1 通过现有内核虚拟内存能力建立 MSI-X table/PBA 的有界不可缓存 MMIO 映射，映射失败返回确定性错误
- [x] 3.2 实现条目编程：message address（LAPIC + 目标 APIC ID）、message data（已分配向量），遵循 mask-先行顺序
- [x] 3.3 实现 per-vector mask/unmask 与 function-level enable/disable，保证编程完成前不投递

## 4. 与向量/EOI 边界整合

- [x] 4.1 通过 `kernel-irq-vector-allocation` 为每个使用条目分配向量并注册 handler，标注 `VectorOwner::Lapic`
- [x] 4.2 确认 MSI-X 向量触发走现有 `InterruptFrame` 分发并经 LAPIC EOI 唯一发送，不触碰 PIC EOI，不改变异常/syscall 语义

## 5. 默认关闭验证入口

- [x] 5.1 新增默认关闭构建开关，使用可控测试设备或可控 producer 触发 MSI-X 向量，验证 capability 解析、条目编程、向量投递 handler 闭环，按既有 COM1/VGA marker 风格输出 pass/fail
- [x] 5.2 验证 mask 抑制投递与 unmask 恢复投递，验证“编程向量 == 实际触发 handler”
- [x] 5.3 添加源级检查：MSI-X 向量 EOI 唯一且为 LAPIC、配置仅在可阻塞上下文、IRQ 路径无阻塞/分配

## 6. 构建与静态检查

- [x] 6.1 运行 `xmake`（x86_64-elf-gcc/g++）确认默认配置与启用验证开关均可编译
- [x] 6.2 运行 clang 与 clangd 辅助静态检查（freestanding C++17、x86_64 target、项目 include、无 hosted/异常/RTTI），修复本次引入 error 并确认/修复有效新增 warning；工具不可用则记录原因与残余风险
- [x] 6.3 区分历史诊断、本次变更诊断与工具链/freestanding 误报

## 7. 仿真器验证与记录

- [x] 7.1 在 QEMU（`--display none`）下运行启用验证开关的 smoke，观察 MSI-X 投递 marker，日志写入 `logs/`
- [x] 7.2 默认启动回归：确认默认关闭验证时 boot/shell/`/rw`/userland baseline 行为不变
- [x] 7.3 记录验证：通过项、因 QEMU/Bochs/MSI-X 支持/工具链不可用而跳过的项与残余风险分别记录，不对跳过环境声明运行时成功

## 8. 中断/驱动安全审查

- [x] 8.1 审查中断安全与重入：MSI-X 编程不在 IRQ 上下文，分发与 EOI 所有权未被破坏，配置窗口无杂散中断
- [x] 8.2 审查硬件访问顺序：mask-先行、message 编程、enable/unmask 的顺序与可见失败路径确定性
