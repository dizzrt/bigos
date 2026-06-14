## 1. Boundary Inventory

- [x] 1.1 盘点 `kernel/core/irq`、`kernel/core/timer`、`kernel/core/sched`、PIC/PIT driver、context-switch assembly 和相关 public headers 的真实调用点，标记 architecture-owned、driver-owned、timer-owned、scheduler-owned 与 context-switch-owned 边界。
- [x] 1.2 记录当前必须保持不变的低层假设：interrupt vectors、syscall vector、i8259 EOI ordering、`InterruptFrame` 语义、context-switch frame layout、boot/linker 地址、page-table layout、disk layout 和用户态 syscall ABI。
- [x] 1.3 识别仍需保留在 x86_64/driver 实现侧的机制，包括 IDT/ISR frame、PIC/PIT port IO、EOI、hardware timer programming 和 assembly register/stack details。
- [x] 1.4 设计并新增极小 architecture-context header 的语义边界，限定其只暴露 scheduler/IRQ-return 需要的上下文语义，不暴露 x86_64 descriptor、裸寄存器 offset、PIC/PIT port IO 或 ISR stack layout。

## 2. IRQ And Timer Boundary

- [x] 2.1 收敛 external IRQ dispatch 与 core policy 的接口，使 CPU exception、external IRQ 和 syscall 的 EOI/return 语义仍由 interrupt dispatch 边界拥有。
- [x] 2.2 保持 PIT IRQ0 只通过 timer-owned IRQ-context-safe API 推进单调 tick，避免 IRQ 层直接修改 timer 内部状态或在 handler 内承担 scheduler policy。
- [x] 2.3 将 timer-to-scheduler hook 限定为 bounded reschedule intent 或 accounting update，确认其不分配、不释放、不阻塞、不调用 delay、不访问文件系统、不依赖用户态服务、不直接发送 EOI。
- [x] 2.4 复查 PIC/PIT port IO 顺序、reentrancy、interrupt safety 和 visible failure 行为，记录硬件访问 ordering 与默认单核假设。
- [x] 2.5 对 EOI ordering 执行 targeted consistency search 和 source-level review，确认 external IRQ dispatch 是单一 EOI owner；仅当 runtime control-flow 变更暴露 review/search 无法重复覆盖的盲区时，新增窄静态检查脚本。

## 3. Scheduler And Context Boundary

- [x] 3.1 收敛 scheduler-facing context-switch 调用点，使 ordinary scheduler policy 通过 architecture-context header 或 scheduler-owned wrapper 消费语义化 context boundary，不 open-code x86_64 raw frame/register offsets。
- [x] 3.2 复查 IRQ-return preemption eligibility，确认 preemption-disable guard、scheduler critical section、fatal diagnostic path、exception path 和 syscall-forbidden region 都会延迟切换。
- [x] 3.3 确认 timer-driven pending reschedule intent 在 protected region 内不会丢失，并会在 deterministic safe scheduler boundary 处理或保留。
- [x] 3.4 复查 IRQ path allocator exclusion，确认 IRQ/timer/scheduler hook 不创建或释放 TCB、kernel stack、run-queue node、wait-queue node 或其他 ordinary scheduler-owned dynamic objects。

## 4. Documentation And Specs

- [x] 4.1 更新必要的架构文档，说明 interrupt/timer/context/scheduler 边界、当前 x86_64-only runnable backend、单核范围和非目标；若更新 `docs/en`，同步对应 `docs/zh` 路径。
- [x] 4.2 如需更新 `roadmap.md`，仅保留项目规划级表述，不加入具体入口点、文件路径、命令、validation marker、源码级实现细节或 archive/version index。
- [x] 4.3 更新 OpenSpec validation notes 或实现记录，区分已通过检查、无法运行的检查及原因、历史诊断、当前变更新诊断和 freestanding/toolchain false positives。

## 5. Static And Build Validation

- [x] 5.1 执行 OpenSpec 状态/规格检查，至少运行 `openspec status --change harden-interrupt-timer-context-boundary`，并在实现完成后运行适用的 strict validation；若工具不可用，记录 blocker。
- [x] 5.2 对边界命名、owner 注释和低层假设执行 targeted consistency search，确认没有把 stage 序号、SMP、新 backend parity、APIC/HPET 或完整 HAL 承诺误写入本 change 范围。
- [x] 5.3 若修改 C++ source/header/build config，尤其新增 architecture-context header，运行最窄可用 `xmake` cross-toolchain build；若缺少 `x86_64-elf-gcc`、`x86_64-elf-g++`、`xmake` 或 disk image 配置，记录无法执行原因和剩余 runtime 风险。
- [x] 5.4 若修改 C++ source/header/build config，运行或记录 clang/clangd 辅助诊断，配置应尽量贴近 freestanding C++17、x86_64 target、project include paths、no exceptions、no RTTI；修复当前变更新引入的有效错误或警告。
- [x] 5.5 若新增或修改 Python helper/test 文件，按需运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若未涉及 Python 文件，不要求 Python lint/type/test。

## 6. Runtime Validation

- [x] 6.1 若 runtime control flow 未改变，仅记录 OpenSpec、consistency search 和 build/static check 结果，并说明 QEMU/Bochs smoke 跳过原因。
- [x] 6.2 若修改 IRQ、PIT/PIC、timer hook、scheduler preemption 或 context-switch runtime path，在环境可用时运行最窄 QEMU headless smoke，验证 default x86_64 Legacy BIOS path 仍可启动并保持相关 timer/scheduler 行为。
- [x] 6.3 自动化和 smoke 测试优先通过 QEMU headless 执行；对 early boot、port IO、EOI ordering、interrupt return 或 context-switch assembly 风险较高的改动，可补充 Bochs 早期手工测试或交叉验证。
- [x] 6.4 若 QEMU headless 因 emulator、ROM、disk image、serial oracle 或 toolchain 问题不可用，记录缺失依赖和自动化 smoke 剩余风险；若 Bochs 不可用，记录其仅影响手工/交叉验证结论。
- [x] 6.5 复核 runtime 验证结果不声称 SMP、UEFI runtime parity、non-x86 backend parity、APIC/IOAPIC、HPET、完整 POSIX scheduling 或 real-time semantics。
