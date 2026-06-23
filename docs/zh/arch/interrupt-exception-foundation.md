# 中断与异常基础设施

BigOS x86_64 中断路径覆盖 CPU exception、`int 0x80` syscall gate、PIC fallback IRQ，以及 APIC-owned local timer/IPI/已支持的 IOAPIC external IRQ。该基础设施用于保持 dispatch 与 EOI ownership 可诊断，不代表已经具备完整输入子系统、CPU hotplug、NUMA、MSI/MSI-X、完整 IRQ affinity 或非 x86_64 interrupt backend parity。

## 初始化顺序

`kernel()` 保持以下顺序：

```text
VGA clear
serial_init()
init_mem()
optional BIGOS_MM_SELF_TEST
optional BIGOS_USER_VMEM_SMOKE
terminal::init_tty()
irq::initIRQ()
optional BIGOS_PAGE_FAULT_SMOKE trigger
irq::enableIRQ()
normal boot marker
optional syscall / scheduler / user-program smoke
sched::start()  (idle thread owns halt; replaces the bare hlt loop)
```

`serial_init()` 在默认 boot path 中显式初始化 COM1，确保普通 serial marker 和 early diagnostics 不再隐式依赖 `BIGOS_MM_SELF_TEST`。`BIGOS_MM_SELF_TEST` 仍在 PIC 初始化和 `sti` 之前运行。普通 allocator、kernel API 和内存自检不承诺 IRQ-context 安全；`kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()` 和全局 `new/delete` 不允许从 IRQ handler 调用。

## Interrupt Guard

`bigos::irq::InterruptGuard` 是单核早期内核的最小 critical-section primitive。构造时读取 RFLAGS.IF 并执行 `cli`；析构时只在进入前 IF 为 enabled 时恢复 `sti`，进入前已 disabled 的路径保持 disabled。该 guard 只防止 same-CPU maskable IRQ interleaving，不是 SMP lock，不保护 NMI，不提供阻塞语义，也不是 scheduler lock。

allocator 内部使用该 guard 保护 buddy、slab 和 KVMEM 的短元数据更新边界。它不会让普通 allocator 变成 IRQ-handler-safe API；后续 IRQ producer 若需要 handoff，仍应使用静态或 boot-time-prepared bounded storage，并明确 overflow/drop 策略。

## IDT 所有权

kernel runtime IDT 使用 kernel-owned static storage，由 `irq::initIRQ()` 构造 gate descriptor 并显式执行 kernel-stage `lidt`。运行期 IDT 不再写入或依赖 legacy 低地址 `IDT_BASE = 0x1000` backing。

该变更不改变以下布局或 ABI：

- boot 固定地址和 Legacy BIOS handoff 地址。
- linker higher-half base `0xffffffff80000000`。
- kernel 物理加载基址 `0x1000000`。
- `BootInfoHeader*` 入口 ABI 和 BootInfo v1/v2 layout。
- boot-stage page table、self-mapping 地址和 direct-map 规划。

## ISR ABI

所有 generated ISR entry 都进入统一 assembly common path。无 error-code vector 会压入 synthetic zero error code；有 error-code vector 保留 CPU push 的原始 error code。assembly path 保存通用寄存器，向 C++ `irq_dispatch(InterruptFrame*)` 传递统一 frame，并在允许返回的 IRQ 路径对称恢复寄存器后执行 `iretq`。

foundation 阶段的 `InterruptFrame.rsp` 是 ring-0 interrupt entry 可计算的 interrupted stack pointer；后续用户态工作已加入 bounded ring3 transition 和 syscall frame 消费者，但本文档仍不定义完整的 architecture-neutral trap-frame ABI。

## Dispatch 策略

`irq_dispatch()` 按显式 ownership 分流：

- CPU exception ownership：不发送 irqchip EOI。
- PIC fallback IRQ ownership：handler 返回后发送一次 i8259 EOI。
- LAPIC/APIC ownership：local timer、IPI 与已支持的 IOAPIC external IRQ 在 handler 返回后发送一次 LAPIC EOI。
- Syscall ownership：vector `0x80` 是 software-interrupt syscall entry，不发送 irqchip EOI。
- Unknown 或 unsupported ownership：输出包含 vector 与已知 owner 分类的 deterministic 诊断。

未注册但已拥有 owner 的 IRQ 会走安全默认 handler，输出 vector 与 owner class，再由 owner-specific EOI 路径完成。

## Page Fault

`#PF` handler 是 diagnostic-only 路径。它读取 `CR2`，输出固定 marker `BIGOS_PAGE_FAULT`、fault address、raw error code，并解码 present、write、user、reserved-bit 和 instruction-fetch 位。输出后进入 `cli; hlt` 循环，不分配内存、不释放内存、不修改页表恢复、不重试 faulting instruction，也不声明 demand paging 支持。

验证专用触发器由 `xmake f --page_fault_smoke=y` 打开。默认 boot 不主动触发 `#PF`。

## Keyboard IRQ1 输入交接

keyboard IRQ1 现在用于受控输入 handoff，而不是在 ISR 中直接输出 smoke marker。初始化会先通过 `terminal::init_tty()` 准备 input ring、console flag 和 keyboard decoder state，再由 `irq::initIRQ()` 注册 vector `0x21` handler。APIC default-delivery 配置下，该 handler 属于 IOAPIC/LAPIC 路径并投递到已初始化且 online 的 BSP；BSP-only fallback 下，该 handler 属于 PIC 路径，并在 handler 注册后 unmask IRQ1。

handler 只读取 PS/2 data port `0x60` 的一个 scancode byte，执行 bounded set-1 decode，并把受支持字符交给 TTY fixed-capacity input buffer。handler 不直接发送 EOI、不调用 `kprintf()`/`kput()`、不写 VGA/serial、不调用 `kmalloc()`/`free()`/`alloc_kernel_pages()`/`free_pages()`/global `new/delete`、不阻塞、不调用 `mdelay()`，也不依赖 filesystem、scheduler、syscall、用户态或 TTY consumer progress。EOI 由 external IRQ dispatch 在 handler 返回后按 owner-specific 路径发送一次。

本路径不是完整输入子系统；多 TTY、阻塞读、shell、用户态输入和完整 keyboard layout 留给后续阶段。

## 验证记录

已通过检查：

- exception dispatch path 不包含 PIC EOI。
- keyboard IRQ1 handler 注册先于 IRQ1 unmask。
- memory self-test 保持在 IRQ/PIC 初始化和 `enableIRQ()` 之前。
- `#PF` handler 读取 `CR2`、输出 `BIGOS_PAGE_FAULT`，且不调用分配、页表恢复或重试路径。
- kernel runtime IDT 使用 static storage 并执行 `lidt`。
- `uv run pytest`：31 passed。
- `xmake`：default build 通过；仍有既有 command-line whitespace warning、`$(buildir)` deprecated warning 和 RWX LOAD segment linker warning。
- `xmake f --mm_self_test=n --page_fault_smoke=y && xmake && xmake f --mm_self_test=n --page_fault_smoke=n`：validation-only `#PF` trigger build 通过，并已恢复默认关闭配置。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/irq/interrupt.cc kernel/core/irq/isr.cc kernel/core/kernel.cc kernel/drivers/irqchip/i8259.cc`：通过。
- IDE diagnostics：`include/irq/interrupt.h`、`kernel/core/irq/interrupt.cc`、`kernel/core/irq/isr.cc`、`kernel/core/kernel.cc` 无诊断。

无法完成的 runtime smoke：

- 普通 boot smoke：`uv run python tools/boot_debug.py run --serial-log log/serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 30` 先因既有 Bochs 进程锁定 `build/test/os.raw` 失败；改用隔离 image 后仍未在 30 秒内生成 serial marker。
- memory self-test runtime smoke：先配置 `xmake f --mm_self_test=y`，再执行 `uv run python tools/boot_debug.py run --image build/test/mem-smoke.raw --serial-log log/mem-smoke.serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED --smoke-timeout 30`，未在 30 秒内生成 serial marker，说明当前本机 Bochs/term GUI/serial 组合无法作为可靠 oracle。
- `#PF` runtime smoke：trigger build 已验证，但由于普通 serial smoke 和 memory self-test serial smoke 均不可观测，未继续声明 `BIGOS_PAGE_FAULT` runtime marker 通过。
- keyboard IRQ1 runtime smoke：未做人工 Bochs 键盘输入；本 change 按设计不扩展 `tools/boot_debug.py` 自动注入键盘输入。

剩余风险：当前源码级检查和交叉构建覆盖了 IDT/ISR/PIC/keyboard/#PF 的关键静态不变量，但普通 boot、page fault halt 和人工 keyboard IRQ delivery 仍需要在可稳定观测 VGA/serial 的 Bochs 环境中复核。
