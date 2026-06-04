# 中断与异常基础设施

BigOS 早期 x86_64 中断路径当前只覆盖单核、Legacy BIOS、i8259 PIC 和 Bochs 验证场景。该基础设施用于让 CPU exception、外部 IRQ 和最小 keyboard smoke 可诊断，不代表已经具备调度器、完整输入子系统、用户态或缺页恢复能力。

## 初始化顺序

`kernel()` 保持以下顺序：

```text
VGA clear
init_mem()
optional BIGOS_MM_SELF_TEST
irq::initIRQ()
optional BIGOS_PAGE_FAULT_SMOKE trigger
irq::enableIRQ()
normal boot marker
hlt loop
```

`BIGOS_MM_SELF_TEST` 仍在 PIC 初始化和 `sti` 之前运行。当前 allocator、kernel API 和内存自检不承诺 IRQ-context 安全。

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

当前 `InterruptFrame` 中的 `rsp` 是 ring-0 interrupt entry 可计算的 interrupted stack pointer；BigOS 尚未实现用户态 privilege transition，因此不声明完整用户态 `SS:RSP` 语义。

## Dispatch 策略

`irq_dispatch()` 按 vector 范围分流：

- `0x00..0x1f`：CPU exception，不发送 PIC EOI。
- `0x20..0x2f`：remapped i8259 external IRQ，handler 返回后发送 EOI。
- 其他 vector：输出 deterministic unknown-vector 诊断并返回。

未注册 external IRQ 会走安全默认 handler，输出 vector/IRQ line 后发送 EOI，避免 PIC 卡住。

## Page Fault

`#PF` handler 是 diagnostic-only 路径。它读取 `CR2`，输出固定 marker `BIGOS_PAGE_FAULT`、fault address、raw error code，并解码 present、write、user、reserved-bit 和 instruction-fetch 位。输出后进入 `cli; hlt` 循环，不分配内存、不修改页表、不重试 faulting instruction，也不声明 demand paging 支持。

验证专用触发器由 `xmake f --page_fault_smoke=y` 打开。默认 boot 不主动触发 `#PF`。

## Keyboard IRQ1 Smoke

keyboard IRQ1 只用于证明外部 IRQ delivery、C++ dispatch 和 i8259 EOI 路径可用。初始化会先注册 vector `0x21` handler，再 unmask i8259 IRQ line 1，其他 line 保持 masked。handler 只读取 PS/2 data port `0x60` 的一个 scancode byte 并输出 `BIGOS_KEYBOARD_IRQ scancode=<value>`，不依赖 TTY/keymap、heap allocation、scheduler、阻塞等待或 hosted runtime API。

本路径不是完整输入子系统。

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
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/kernel/irq/interrupt.cc src/kernel/irq/isr.cc src/kernel/kernel.cc src/drivers/irqchip/i8259.cc`：通过。
- IDE diagnostics：`include/irq/interrupt.h`、`src/kernel/irq/interrupt.cc`、`src/kernel/irq/isr.cc`、`src/kernel/kernel.cc` 无诊断。

无法完成的 runtime smoke：

- 普通 boot smoke：`uv run python tools/boot_debug.py run --serial-log build/test/serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 30` 先因既有 Bochs 进程锁定 `build/test/os.raw` 失败；改用隔离 image 后仍未在 30 秒内生成 serial marker。
- memory self-test runtime smoke：`uv run python tools/boot_debug.py run --image build/test/mem-smoke.raw --serial-log build/test/mem-smoke.serial.log --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED --smoke-timeout 30` 未在 30 秒内生成 serial marker，说明当前本机 Bochs/term GUI/serial 组合无法作为可靠 oracle。
- `#PF` runtime smoke：trigger build 已验证，但由于普通 serial smoke 和 memory self-test serial smoke 均不可观测，未继续声明 `BIGOS_PAGE_FAULT` runtime marker 通过。
- keyboard IRQ1 runtime smoke：未做人工 Bochs 键盘输入；本 change 按设计不扩展 `tools/boot_debug.py` 自动注入键盘输入。

剩余风险：当前源码级检查和交叉构建覆盖了 IDT/ISR/PIC/keyboard/#PF 的关键静态不变量，但普通 boot、page fault halt 和人工 keyboard IRQ delivery 仍需要在可稳定观测 VGA/serial 的 Bochs 环境中复核。
