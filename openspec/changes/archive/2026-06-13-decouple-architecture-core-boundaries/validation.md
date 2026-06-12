# Validation Notes

## Boundary Inventory

- 启动交接：`kernel/arch/x86/boot` 负责 BIOS/MBR/exFAT boot sector、long-mode transition、早期页表、ATA PIO loader、BootInfo v1/v2 producer；核心入口只消费 `BootInfoHeader*` handoff。
- Exception/IRQ/syscall：`kernel/core/irq` 当前拥有 IDT setup、ISR assembly frame、CR2 读取、exception/IRQ/syscall dispatch 和 i8259 EOI 分流；`include/irq/interrupt.h` 仍是当前 x86_64 interrupt ABI boundary。
- 调度上下文切换：`kernel/core/sched` 消费 scheduler stack pointer、IRQ-return preemption 和 address-space restore；`kernel/core/sched/switch.s` 保留 AMD64 callee-saved context-switch frame。
- Memory/page-table：`kernel/mm` 当前实现 4-level PML4、recursive self-map、direct map、PTE bit layout、`invlpg` 和 CR3 read/write；没有新增 alternate paging backend。
- User-mode entry：`kernel/core/proc` 和 `include/bigos/user_mode.h` 消费 GDT/TSS setup、TSS `rsp0`、`iretq` user entry、syscall/signal/fork frame layout。
- PIC/PIT/VGA/ATA：`kernel/drivers` 与 driver-facing core path 持有 i8259、PIT、VGA text、COM1、CMOS RTC、keyboard scancode 和 ATA PIO port 常量。

## Classification

- 核心概念：boot handoff pointer、interrupt dispatch、timer tick、scheduler policy、address-space root activation、user context entry/resume、VFS/block consumption、bounded syscall dispatch。
- x86_64 backend 实现：Legacy BIOS boot stages、long-mode transition、GDT/TSS/IDT setup、CR2/CR3 operations、PML4 layout、recursive mapping、`iretq` frames、AMD64 switch frames。
- 设备驱动实现：i8259 EOI/mask/spurious handling、PIT programming、VGA text memory and CRT ports、COM1 serial, CMOS RTC, keyboard scancode stream, ATA PIO ports。
- 构建/链接约束：higher-half kernel base、`link.lds` entry and segments、raw exFAT boot image layout、Legacy BIOS run targets, x86_64 cross toolchain。

## Preserved Assumptions

- 未改变 boot/linker 地址、BootInfo fixed/v2 handoff ABI、higher-half base、disk image layout 或 Legacy BIOS/MBR/exFAT runnable backend。
- 未改变 IDT vector、exception/IRQ/syscall dispatch rules、syscall no-EOI rule、interrupt frame layout、scheduler switch frame layout 或 syscall register ABI。
- 未改变 page-table layout、recursive self-map、direct map、CR3 semantics、TLB invalidation、buddy/slab/kmalloc 初始化顺序或 allocation phase。
- 未新增 UEFI、non-x86 backend、speculative HAL、SMP、宽泛设备模型或新 storage driver。

## Source Cleanup Review

- `include/bigos/memory.h`、`kernel/mm/kmem.h`、`kernel/mm/buddy.h`、`kernel/mm/vmem.h` 和 `kernel/core/kernel.cc` 不再为了 opaque `BootInfoHeader*` 声明包含 `include/arch/x86/boot/boot_info.h`。
- `kernel/mm/vmem.cc` 显式包含 `include/arch/x86/boot/boot_info.h`，因为它解析 BootInfo memory-map sections 并消费具体 x86 boot ABI。
- 该 cleanup 只调整 include direction，不改变 runtime control flow、IRQ state、port I/O、MMIO、driver state、allocation behavior 或 page-table mutation order。
- Interrupt safety review：没有新增 IRQ handler 调用、EOI 行为、interrupt enable/disable 范围、port I/O 顺序或 ISR frame 访问。
- Memory review：没有新增 allocation path、释放 path、page-table update、alignment 规则、metadata lifetime 或 early memory phase 变更。

## Searches

- Targeted coupling inventory covered `kernel/core`, `kernel/mm`, `include`, `kernel/drivers`, and `kernel/arch/x86/boot` for `arch/x86`, `CR3`, `GDT`, `TSS`, `IDT`, descriptor, port I/O, inline asm, interrupt frame, boot handoff, and x86_64 ABI terms.
- Historical leakage remains by design in current x86_64 ABI boundaries: `include/irq/interrupt.h`, `include/bigos/user_mode.h`, page-table APIs in `include/bigos/memory.h`, syscall frame consumption, and driver port I/O APIs.
- Current change reduces one include-direction leak for opaque BootInfo handoff declarations and records remaining x86_64-only facts in bilingual docs.

## Validation Results

- `openspec status --change decouple-architecture-core-boundaries --json`: proposal, design, specs, and tasks artifacts reported `done`.
- Targeted include search after cleanup found `#include <arch/x86/boot/boot_info.h>` only in `kernel/mm/buddy.cc` and `kernel/mm/vmem.cc`, where concrete BootInfo parsing is required.
- Targeted documentation/OpenSpec search found only non-goal or requirement wording for runnable multi-architecture support; no doc claims BigOS has runnable multi-architecture support.
- Targeted core/mm/include mechanism search still finds historical x86_64 ABI boundaries in interrupt, syscall, process, scheduler, and memory paths. Those are current baseline facts documented in `docs/en/arch/architecture-core-boundaries.md` and `docs/zh/arch/architecture-core-boundaries.md`, not new leakage from this change.
- `xmake`: passed. The linker still emitted `build/kernel has a LOAD segment with RWX permissions`; this is an existing build warning unrelated to the include-direction cleanup and should be handled by a separate linker/layout change.
- `GetDiagnostics`: no diagnostics reported after edits.
- QEMU/Bochs runtime smoke was not run because this change adjusts include direction and documentation only; it does not change runtime boot, IRQ, timer, scheduler context switch, memory mapping, syscall, user-mode entry, or hardware driver behavior.
