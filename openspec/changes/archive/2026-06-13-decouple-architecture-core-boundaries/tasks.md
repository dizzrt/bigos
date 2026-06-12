## 1. Boundary Inventory

- [x] 1.1 盘点启动交接、exception/IRQ/syscall、调度上下文切换、memory/page-table、user-mode entry、PIC/PIT/VGA/ATA 路径中的 x86_64 专属消费点。
- [x] 1.2 标记每个消费点属于核心概念、x86_64 backend 实现、设备驱动实现或构建/链接约束。
- [x] 1.3 搜索核心层对 x86_64 私有头、裸寄存器布局、descriptor、CR3/GDT/TSS/IDT 细节、port IO 常量和汇编 frame 细节的直接依赖。
- [x] 1.4 记录不得改变的 boot/linker 地址、interrupt vector、page-table layout、disk layout、context-switch frame 和 syscall ABI 假设。

## 2. Core/Architecture Interface Cleanup

- [x] 2.1 为已确认的真实消费点收敛最小架构边界接口，确保接口名称表达核心语义而不是 x86_64 机制。
- [x] 2.2 将 x86_64 寄存器、descriptor、CR3、GDT/TSS/IDT、汇编 entry/frame 和硬件常量保持在 x86_64 backend 或驱动实现侧。
- [x] 2.3 调整核心层 include 方向，避免 `kernel/core`、`kernel/mm` 和公开 kernel headers 随意包含 x86_64 私有实现细节。
- [x] 2.4 保持现有 x86_64 Legacy BIOS/MBR/exFAT backend 为唯一 runnable backend，不新增空 backend 或 speculative HAL。
- [x] 2.5 对触及 IRQ、port IO、MMIO 或驱动状态的改动执行 interrupt safety、reentrancy、hardware-access ordering 和 failure behavior review。
- [x] 2.6 对触及 buddy、slab、kmalloc、virtual memory 或 early memory 初始化的改动执行初始化顺序、allocation phase、object lifetime、alignment 和 failure behavior review。

## 3. Documentation And Spec Alignment

- [x] 3.1 更新必要的 `docs/en` 文档，明确 x86_64/core 边界纪律、唯一 runnable backend 和未来多架构非目标。
- [x] 3.2 同步更新对应 `docs/zh` 文档，保持目录结构与英文镜像一致。
- [x] 3.3 如需更新 `roadmap.md`，保持规划级表述，不加入具体入口点、文件路径、命令、validation marker、源码级实现细节或 archive/version index。
- [x] 3.4 检查文档和 OpenSpec artifacts，确保没有把 x86_64-only baseline 描述为 runnable multi-architecture support。

## 4. Validation

- [x] 4.1 运行 `openspec status --change decouple-architecture-core-boundaries`，确认 proposal、design、specs 和 tasks 均完成。
- [x] 4.2 运行 targeted consistency search，确认核心层没有新增 x86_64 私有机制泄漏；若发现历史遗留项，区分历史问题与当前 change 引入的问题。
- [x] 4.3 对 C/C++ 源码或头文件改动运行最窄可用 `xmake` 交叉工具链构建；若 `x86_64-elf-gcc`、`xmake` 或本地配置不可用，记录 blocker 和剩余风险。
- [x] 4.4 对 C++ 源码、头文件、KTL、kernel C++ support 或 C++ build 配置改动运行接近 freestanding C++17/x86_64 cross-build 的 clang/clangd 辅助检查，或记录工具/flag 缺口与风险。
- [x] 4.5 若改动触及 boot、IRQ、timer、scheduler context switch、memory mapping、syscall、user-mode entry 或 hardware driver runtime path，优先运行 QEMU headless smoke；环境支持时对早期 boot、port IO 或硬件行为风险补充 Bochs 或 QEMU/Bochs cross-validation。
- [x] 4.6 汇总 validation notes，分开记录已通过检查、无法运行的检查及原因、历史 diagnostics、当前 change 引入并已修复的问题和剩余风险。
