## Why

当前最小可用系统已经具备用户地址空间、进程生命周期、demand paging、COW、`execve` 和 ring3 入口，但 VM 策略、地址空间切换、用户态入口与架构特定 fault handling 的责任边界仍容易在后续扩展中被混合。
现在需要把这些边界收敛成可审查的核心契约，避免后续 backend 扩展、用户内存能力或 fault 处理工作意外改变现有 x86_64 运行路径。

## What Changes

- 明确 core virtual-memory policy、VMA/page-table materialization、CR3/address-space activation、user-entry mechanics 和 architecture-specific fault dispatch 的责任分层。
- 收敛核心层对用户入口、地址空间切换和 fault 分类的消费方式，避免在通用核心策略中扩散 x86_64 私有寄存器、frame、GDT/TSS/CR3 或 page-table 细节。
- 保持现有 boot/linker/page-table/self-mapping/direct-map/syscall vector/user stack/disk layout 假设不变；如需改变这些低层 ABI，必须拆成独立 change。
- 约束用户态 fault recovery 只覆盖现有已实现的 VMA-backed demand-zero/COW/stack-growth 等有界路径，unsupported kernel fault 仍进入现有 diagnostic 或 panic 边界。
- 为源码整理、文档同步和验证记录建立分层要求，确保 spec/documentation-only、source refactor、runtime behavior touch 三类工作有匹配的验证强度。
- 非目标：不实现 broad file-backed `mmap`、dynamic linking/shared libraries、第二架构 runtime parity、UEFI backend、SMP、完整 POSIX process model、完整 libc 或新的存储/device backend。

## Capabilities

### New Capabilities
- `vm-user-entry-boundary`: 覆盖 BigOS 当前 x86_64 baseline 下 VM policy、地址空间激活、用户态入口和 fault handling 的核心/架构边界契约。

### Modified Capabilities
- `architecture-core-boundary-discipline`: 补充 VM/user-entry/fault 边界作为架构解耦纪律中的真实消费点，并保持 current runnable backend 与低层 ABI 假设不变。

## Impact

- 受影响子系统：`kernel/mm` 的页表和 VM policy 边界、`kernel/core/proc` 的地址空间切换与用户入口、`kernel/core/irq` 的 fault dispatch、`kernel/core/syscall` 的 user/kernel 过渡，以及相关公开 headers 和架构实现边界。
- 架构假设：当前 runnable backend 仍是 x86_64 Legacy BIOS/MBR/exFAT；本 change 不要求 UEFI、non-x86 backend、APIC/IOAPIC、SMP 或新 boot path。
- 内存布局假设：保持 higher-half kernel、direct map、KVMEM、recursive/self-mapping、用户低半区隔离、CR3 切换语义和现有 user stack/layout 不变。
- 工具链与模拟器假设：实现阶段优先使用 xmake 与 `x86_64-elf-gcc` 做最窄构建检查；环境支持时使用 QEMU headless 验证相关用户态/VM 行为，Bochs 仅作为早期 boot 或硬件行为风险较高时的补充交叉验证。
- 文档影响：如更新 `docs/en` 中架构、VM、user-entry 或 fault 边界说明，必须同步更新对应 `docs/zh`；`roadmap.md` 仍只保留项目规划级表述。
