## Context

BigOS 当前处于 kernel infrastructure bring-up 阶段。启动路径已经进入 long mode，并在 kernel 入口中完成 VGA 清屏、`init_mem()`、可选 memory self-test 和 `initIRQ()`，但 `enableIRQ()` 仍被注释，说明 IRQ 尚未作为正常运行路径启用。现有 IRQ 代码包含 assembly ISR entry、IDT descriptor、默认 ISR、i8259 PIC 驱动和 ISR 注册接口，但它们仍存在几个基础不确定性：

- kernel 阶段写入 `IDT_BASE` 后没有明确的 `lidt` 重新加载契约。
- assembly stub 传递给 C++ 的寄存器和 error code 语义不够显式。
- CPU exception 与 external IRQ 共享默认路径，异常可能被错误发送 PIC EOI。
- `#PF` 只有默认 ISR 输出，缺少 `CR2`、error code 和稳定 panic/halt 行为。
- PIC remap/mask/unmask 存在驱动函数，但 kernel 初始化路径未完整调用。
- keyboard ISR 在声明和注释中存在，但没有完整接线，不应继续让 README/配置假设它已经可用。

本 change 在内存三件套完成后插队进行，目的是先让异常和外部 IRQ 成为可诊断、可验证的早期内核基础设施。它仍保持单核、early kernel、无 scheduler、无 SMP、无用户态地址空间、无 filesystem 的约束。

目标初始化流如下：

```text
kernel()
  -> vga::clear_screen()
  -> init_mem(boot_info)
  -> optional mm::self_test()      # 仍在 IRQ disabled 环境
  -> irq::initIRQ()
       -> build kernel IDT
       -> lidt kernel IDT pointer
       -> install exception handlers
       -> init i8259 PIC, keep IRQs masked by default
       -> register keyboard IRQ1 handler
       -> unmask keyboard IRQ1 only when handler is ready
  -> irq::enableIRQ()
  -> print "BigOS kernel reached"
  -> hlt loop
```

## Goals / Non-Goals

**Goals:**

- 建立稳定的 x86_64 kernel IDT 初始化和加载路径。
- 让 ISR entry 向 C++ 分发层传递统一 interrupt frame，覆盖 vector、error code、通用寄存器和返回上下文中当前可安全保存的字段。
- 区分 CPU exception、i8259 external IRQ 和保留/未知 vector。
- 实现诊断型 `#PF` handler，读取 `CR2`，输出固定 marker、fault address、error code 位含义并 halt。
- 接入 i8259 初始化、EOI 和 keyboard IRQ1 unmask 顺序。
- 提供最小 keyboard IRQ handler：读取 data port `0x60`，输出 scancode marker 或临时 VGA/serial 可观测结果。
- 更新源码级测试或静态检查，防止 exception 路径发送 PIC EOI、keyboard handler 未注册就 unmask、memory self-test 被移动到 IRQ enable 之后。
- 提供 Bochs smoke 验证路径或明确记录不可运行原因。

**Non-Goals:**

- 不实现 scheduler、线程、抢占、阻塞等待或中断上下文锁策略。
- 不实现 APIC、IOAPIC、MSI/MSI-X 或 SMP TLB shootdown。
- 不实现用户态、syscall、process、ELF 用户程序加载或用户地址空间。
- 不实现 demand paging、copy-on-write、page fault recovery 或 lazy page table allocation。
- 不实现完整 TTY/console 输入栈，keyboard IRQ 只提供最小 scancode smoke。
- 不改变 boot 固定地址、linker higher-half base、kernel load base、BootInfo handoff ABI、self-mapping 地址或 direct-map 规划。

## Decisions

### Decision: kernel 阶段显式加载 IDT

`irq::initIRQ()` SHALL 在写入 IDT descriptor 后显式执行 `lidt`，使 kernel 阶段 IDT 状态不依赖 boot 阶段 `lidt` 的遗留 descriptor。IDT storage SHALL 迁移为 kernel-owned static IDT storage，不再把 kernel IDT descriptor 写入固定低地址 `IDT_BASE = 0x1000`。实现可以保留 legacy `IDT_BASE` 常量用于审查或兼容说明，但 kernel runtime 不应依赖该低地址作为当前 IDT backing。

选择 kernel-owned static storage 的原因是：低地址 `0x1000` 属于 boot/handoff/page-table 布局的高风险区域，继续复用会让后续 boot layout、direct map、page table ownership 和 exception bring-up 相互耦合。static IDT 由 kernel 镜像持有，生命周期清晰，且不改变对外 BootInfo handoff ABI 或 linker higher-half base。

替代方案是继续复用 boot 阶段 IDT。该方案隐藏初始化依赖，后续启用 IRQ 时难以判断 handler 表是否已经完整安装，因此不采用。

### Decision: 使用统一 interrupt frame 分发

assembly stub SHALL 保存必要通用寄存器，并向 C++ dispatch 传递 `InterruptFrame*` 或等价结构。该结构至少包含 vector、error code、RIP、CS、RFLAGS、RSP、SS 中当前可获得且正确的字段，并允许 C++ 根据 vector 判断 exception 或 external IRQ。

无 error-code vector 由 stub 填入 error code 0；有 error-code vector 保留 CPU push 的真实 error code。`#PF`、`#GP`、`#DF` 等有 error-code 异常必须走同一 ABI，而不是每个 handler 自行猜测栈布局。

替代方案是保留当前 `(irq_num, ecode)` 两参数接口。它足以打印默认日志，但无法可靠诊断 `#PF`，也不利于后续扩展异常上下文，因此不作为目标设计。

### Decision: exception 与 external IRQ 分流

C++ dispatch SHALL 根据 vector 范围分流：

```text
0x00..0x1f  CPU exception
0x20..0x2f  i8259 external IRQ after PIC remap
other       reserved/unknown vector
```

CPU exception handler MUST NOT 发送 PIC EOI。i8259 IRQ handler 在调用已注册 handler 后发送 EOI；若 handler 未注册，也必须以安全默认 handler 记录 vector 并发送 EOI，避免 PIC 卡住。spurious IRQ7/IRQ15 是否精确检测可留给后续 change，但本 change 至少不能对 exception 发送 EOI。

替代方案是在 assembly 里按 vector 直接发送 EOI。当前代码已经接近这种模式，但它耦合硬件控制器语义和 CPU exception 语义，容易误伤异常路径，因此迁移到 C++ dispatch。

### Decision: `#PF` 只做诊断和 halt

`#PF` handler SHALL 读取 `CR2`，输出专属固定 marker `BIGOS_PAGE_FAULT`，并打印 fault address、error code 以及 present/write/user/reserved/instruction-fetch 等位的含义。处理完成后进入安全 halt，不尝试修复页表、不重试 fault instruction。

`BIGOS_PAGE_FAULT` 暂不并入通用 early panic marker 命名体系。统一 panic/diagnostic 机制属于后续 `unify-early-memory-diagnostics` 或等价 change 的范围；本 change 只需要一个可由 emulator smoke 观测的异常诊断 oracle。

替代方案是立即实现缺页恢复或按需映射。该方案需要 direct map、页表 ownership、用户/内核地址空间边界和 future page fault policy，本 change 不具备这些前置条件。

### Decision: keyboard IRQ 是最小 smoke，不是输入子系统

keyboard IRQ1 handler SHALL 读取 PS/2 data port `0x60`，输出 scancode marker，并发送 EOI。它 MAY 使用临时 VGA 或 COM1 输出，但 MUST NOT 依赖 TTY、scheduler、heap allocation、阻塞等待或完整 keymap。

本 change 不扩展 `tools/boot_debug.py` 来自动注入键盘输入。keyboard smoke 的 runtime 验证先采用人工 Bochs 输入或现有 emulator 能力；如果本地无法稳定注入键盘事件，验证记录必须说明缺口和剩余风险。自动键盘输入注入可作为后续测试工具增强 change 单独规划，避免把 IRQ runtime bring-up 和 boot debug 工具能力耦合。

替代方案是先实现 TTY/console 再接键盘。该方案会扩大范围并把中断基础设施和输入栈耦合；本 change 只需要证明 external IRQ 能到达、分发和返回。

### Decision: memory self-test 保持在 IRQ enable 之前

已有 memory runtime validation 需要保持单核、关中断环境。`kernel()` 初始化顺序必须保证 `mm::self_test()` 在 `irq::enableIRQ()` 之前运行，并且 keyboard/PIC 初始化不能改变 memory self-test 的前置条件。

替代方案是将 memory self-test 放到 IRQ 启用后验证重入安全。当前 allocator 仍不承诺 IRQ context 安全，该方案会引入错误假设，因此不采用。

## Risks / Trade-offs

- [Risk] 修改 ISR assembly ABI 可能破坏 `iretq` 栈恢复或寄存器保存顺序 -> Mitigation: 先用源码级测试锁定有/无 error-code vector 处理和保存/恢复序列，再做 Bochs boot smoke。
- [Risk] 从低地址 `IDT_BASE` 迁移到 kernel static IDT storage 可能暴露 boot 阶段和 kernel 阶段 IDT 假设差异 -> Mitigation: 在 `irq::initIRQ()` 中显式 `lidt` kernel IDT，并用源码级检查确认 kernel 不再写入低地址 IDT backing。
- [Risk] 过早启用 `sti` 可能让未准备好的 IRQ 进入默认路径 -> Mitigation: PIC 初始化后默认 mask all，只在 keyboard handler 注册成功后 unmask IRQ1。
- [Risk] keyboard IRQ smoke 受 Bochs 配置和输入注入方式影响 -> Mitigation: 将 keyboard smoke 设计为可选 runtime 验证；无输入注入能力时记录无法覆盖的风险。
- [Risk] `#PF` 人工触发可能影响普通 boot smoke -> Mitigation: 用编译开关或测试入口触发诊断型 page fault，默认 boot 不主动触发。
- [Risk] 诊断输出依赖 VGA/COM1 早期 IO 能力 -> Mitigation: 复用已有 `kprintf` 和 memory self-test marker 输出路径，不引入新 logging subsystem。

## Migration Plan

1. 先整理 public/internal IRQ header，定义 vector 常量、IRQ line 常量、interrupt frame 和 handler 类型。
2. 重构 `interrupt.s`，统一有/无 error-code vector 的栈布局和 C++ dispatch 调用。
3. 更新 `interrupt.cc`，构建 IDT、执行 `lidt`、注册默认 exception/IRQ handler。
4. 接入 i8259 初始化，移动 EOI 发送到 C++ external IRQ dispatch。
5. 新增 `#PF` 诊断 handler 和可选 fault trigger，确认默认 boot 不触发。
6. 新增 keyboard IRQ1 handler，注册成功后 unmask IRQ1，再允许 `kernel()` 调用 `enableIRQ()`。
7. 增加源码级测试、构建、IDE diagnostics 和 Bochs smoke 验证。
8. 若启用 IRQ 后 boot 不稳定，优先回退 `enableIRQ()` 接入点，保留 IDT/exception 诊断重构以便继续调试。

## Resolved Decisions

- keyboard smoke 不扩展 `tools/boot_debug.py` 自动注入键盘输入；先使用人工 Bochs 输入或现有 emulator 能力，无法验证时记录缺口和剩余风险。
- kernel 阶段 IDT 迁移为 kernel-owned static IDT storage，不再依赖低地址 `IDT_BASE = 0x1000` 作为 runtime IDT backing。
- `#PF` 诊断暂用专属固定 marker `BIGOS_PAGE_FAULT`；通用 early panic marker 命名体系留给后续 diagnostics change。
