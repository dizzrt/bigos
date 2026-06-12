## 1. 现状审查与边界确认

- [x] 1.1 复查 `kernel/core/kernel.cc`、`kernel/core/irq/interrupt.s`、`kernel/core/irq/interrupt.cc`、`kernel/core/irq/isr.cc`、`include/irq/interrupt.h`、`include/irq/isr.h`，确认当前 `initIRQ()`、ISR entry、handler table 和 `enableIRQ()` 接入点。
- [x] 1.2 复查 `kernel/drivers/irqchip/i8259.cc` 与 `include/drivers/irqchip/i8259.h`，确认 PIC remap base、mask/unmask 和 EOI 常量与调用语义。
- [x] 1.3 审查 `IDT_BASE`、boot 阶段 `lidt`、低地址保留区、BootInfo handoff、boot-stage page table 和 kernel image 布局，确认 kernel runtime IDT 迁移到 kernel-owned static IDT storage 后不破坏 boot layout 假设。
- [x] 1.4 明确本 change 不改变 boot 固定地址、linker higher-half base、kernel load base、BootInfo ABI、self-mapping 地址或 direct-map 规划，并把结论记录到实现说明或验证记录。

## 2. IDT 与 ISR ABI 重构

- [x] 2.1 在 IRQ public/internal header 中定义 vector 常量、i8259 vector base、IRQ line 常量、page fault vector 和 keyboard IRQ/vector 映射。
- [x] 2.2 定义 `InterruptFrame` 或等价 C++ dispatch 参数结构，覆盖 vector、error code、必要通用寄存器和可安全读取的返回上下文字段。
- [x] 2.3 重构 `kernel/core/irq/interrupt.s`，统一无 error-code 和有 error-code vector 的栈布局，确保 C++ dispatch 能可靠读取 vector 和 error code。
- [x] 2.4 调整寄存器保存/恢复和 `iretq` 路径，确保允许返回的 IRQ handler 不破坏调用前寄存器状态。
- [x] 2.5 在 `kernel/core/irq/interrupt.cc` 中建立 C++ dispatch 入口，按 CPU exception、i8259 external IRQ 和 unknown vector 分流。
- [x] 2.6 为 kernel-owned static IDT descriptor 初始化补齐 gate 属性、selector、offset 和 reserved 字段，并在 `initIRQ()` 中显式执行 kernel-stage `lidt`。

## 3. Exception 与 `#PF` 诊断

- [x] 3.1 实现默认 CPU exception handler，输出 vector、error code 和可用上下文，并对不可恢复 exception 安全 halt。
- [x] 3.2 实现 `#PF` handler，读取 `CR2`，输出专属 `BIGOS_PAGE_FAULT` marker、fault address 和 raw error code，不接入通用 early panic marker 命名体系。
- [x] 3.3 解码 `#PF` error code 中 present、write、user、reserved-bit、instruction-fetch 位，并在诊断输出中明确含义。
- [x] 3.4 添加 validation-only page fault trigger 开关或入口，确保默认 boot 不主动触发 `#PF`，但 smoke 可验证诊断路径。
- [x] 3.5 确认 CPU exception 路径不发送 i8259 EOI，不分配内存，不尝试修改页表或恢复 faulting instruction。

## 4. i8259 PIC 与 External IRQ

- [x] 4.1 将 i8259 初始化接入 `irq::initIRQ()` 或明确的 IRQ 初始化子流程，保持 master base `0x20`、slave base `0x28`。
- [x] 4.2 初始化 PIC 后默认 mask all，只有在对应 handler 注册完成后才 unmask 目标 IRQ line。
- [x] 4.3 将 i8259 EOI 从 assembly 通用路径迁移到 C++ external IRQ dispatch，确保 IRQ handler 完成后发送 EOI。
- [x] 4.4 为未注册 external IRQ 提供安全默认 handler，输出 vector/IRQ line 并发送 EOI，避免 PIC 卡住。
- [x] 4.5 审查 IRQ handler 的 freestanding 和 interrupt-safety 边界，确认不引入 heap allocation、阻塞等待、scheduler 依赖或 hosted runtime API。

## 5. Keyboard IRQ1 Smoke

- [x] 5.1 新增或接入 keyboard IRQ1 handler 文件，注册到 vector `0x21` 或 IRQ line 1 对应入口。
- [x] 5.2 在 keyboard handler 中读取 PS/2 data port `0x60` 的一个 scancode byte，并输出可观测 marker 或临时 VGA/COM1 日志。
- [x] 5.3 确认 keyboard handler 不依赖完整 TTY/keymap，不做动态分配，不阻塞等待更多输入。
- [x] 5.4 在 keyboard handler 注册成功后 unmask i8259 IRQ1，并确认其他 IRQ line 仍保持 masked。
- [x] 5.5 更新 README 或架构文档中的 IRQ/keyboard 状态说明，避免把最小 smoke 描述成完整输入子系统。

## 6. Kernel 初始化顺序

- [x] 6.1 调整 `kernel/core/kernel.cc`，保持顺序为 VGA -> `init_mem()` -> optional `mm::self_test()` -> IRQ/PIC/keyboard 初始化 -> `enableIRQ()` -> normal boot marker。
- [x] 6.2 确认 `BIGOS_MM_SELF_TEST` 路径仍在 IRQ disabled 环境执行，并且 PIC/keyboard 初始化不会提前发生。
- [x] 6.3 为 `enableIRQ()` 接入点添加清晰注释或封装，说明当前只启用已注册的早期 IRQ smoke，不代表 allocator 或 kernel API 具备 IRQ-context 安全性。
- [x] 6.4 如果启用 IRQ 后 boot smoke 不稳定，优先保留 IDT/exception 诊断改动，并回退或编译开关保护 `enableIRQ()` 默认路径。

## 7. 源码级测试与静态检查

- [x] 7.1 新增或更新 `tests/` 源码级测试，检查 exception vector 路径不包含 PIC EOI 发送逻辑。
- [x] 7.2 新增或更新 `tests/` 源码级测试，检查 keyboard IRQ1 handler 注册先于 IRQ1 unmask。
- [x] 7.3 新增或更新 `tests/` 源码级测试，检查 memory self-test 调用仍位于 IRQ/PIC 初始化和 `enableIRQ()` 之前。
- [x] 7.4 新增或更新 `tests/` 源码级测试，检查 `#PF` handler 读取 `CR2`、输出 `BIGOS_PAGE_FAULT`，并且不调用页分配或页表恢复路径。
- [x] 7.5 运行相关 Python 测试时使用 `uv run pytest`；如果 `uv` 不可用，记录阻塞原因而不是静默使用系统 Python。

## 8. 构建、诊断与 Runtime 验证

- [x] 8.1 运行最窄有效的 `xmake` 或等价 `x86_64-elf-gcc/x86_64-elf-g++` 交叉构建，确认 C++ 和 assembly 改动可构建。
- [x] 8.2 使用 clang 或等价辅助命令检查修改过的 C++ 源/头文件；记录历史诊断、当前 change 新增诊断和 freestanding/toolchain false positive。
- [x] 8.3 使用 clangd 或 IDE diagnostics 检查修改过的 C++ 源/头文件，修复当前 change 引入的有效 error，并确认或修复有效 warning。
- [x] 8.4 在 Bochs 环境可用时运行普通 boot smoke，确认 IRQ enabled 后仍能观察 normal boot marker。
- [x] 8.5 在 Bochs 环境可用且 validation-only page fault trigger 启用时运行 `#PF` smoke，确认 `BIGOS_PAGE_FAULT` marker 可观测且 kernel 安全 halt。
- [x] 8.6 使用人工 Bochs 输入或现有 emulator 输入能力验证 keyboard IRQ1 smoke，确认 scancode marker 可观测；本 change 不扩展 `tools/boot_debug.py` 自动注入键盘输入。
- [x] 8.7 如果 Bochs、ROM、人工 keyboard smoke 或 page fault smoke 无法运行，记录缺失依赖、已通过的替代检查和剩余 bootability/IRQ 风险。

## 9. 验收与归档准备

- [x] 9.1 运行 `openspec validate stabilize-interrupt-exception-foundation --strict`，修复 proposal/design/spec/tasks 的结构问题。
- [x] 9.2 对照 `interrupt-exception-foundation` spec，确认每个 Requirement 至少有实现、测试或明确验证记录。
- [x] 9.3 对照 `kernel-memory-runtime-validation` delta，确认 memory self-test 的 IRQ disabled 边界未被破坏。
- [x] 9.4 汇总验证记录，分离已通过检查、无法运行检查及原因、历史诊断、当前 change 引入并已修复的问题、剩余风险。
- [x] 9.5 更新相关文档或 roadmap 注记，说明本 change 只完成中断/异常基础设施，不代表支持完整输入子系统或缺页恢复。
