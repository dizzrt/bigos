## 1. Boundary Audit

- [x] 1.1 盘点 `kernel/mm`、`kernel/core/proc`、`kernel/core/irq`、`kernel/core/syscall`、`include` 和 x86_64 低层入口中 VM/user-entry/fault 相关调用点，标记 core-owned policy、architecture-owned mechanism、process-owned lifecycle 和 safe teardown 边界。
- [x] 1.2 确认当前 boot/linker/higher-half/direct-map/KVMEM/recursive mapping/syscall vector/IDT vector/user stack/disk layout/CR3 switching 假设，并记录本 change 不修改这些低层 ABI。
- [x] 1.3 识别核心层直接消费 x86_64 私有 CR3、GDT/TSS、interrupt frame、`iretq` frame 或 PTE bit 编码的调用点，区分必须保留的低层实现与可收敛的核心依赖。
- [x] 1.4 盘点现有 VMA、page-table materialization、demand-zero、COW、stack-growth、`execve` rollback、user-buffer validation 和 safe reaper 的一致性边界。

## 2. Core Boundary Implementation

- [x] 2.1 在不改变现有低层 ABI 的前提下，新增独立且极小的 architecture VM/user-entry boundary header，让核心层通过 VM policy、address-space activation、user-entry 和 user-fault 语义消费相关能力。
- [x] 2.2 将 x86_64 私有 CR3 写入、TLB effect、GDT/TSS/RSP0、segment selector、`iretq` frame、assembly restore order 和 page-table bit 编码保留在 architecture-owned 或明确命名的低层实现内。
- [x] 2.3 确保 VMA/process policy 是用户虚拟内存授权来源，page-table materialization 不绕过 VMA 权限，并保持 user-buffer validation、fault recovery 和 user-copy 对 VMA 与 PTE 的一致判断。
- [x] 2.4 确保 address-space activation、active-root restore、process kernel stack release、user-owned page-table teardown 和 VMA metadata cleanup 不在 unsafe return path 上释放仍被当前执行路径依赖的资源。
- [x] 2.5 保持 user-entry 行为只暴露核心语义，避免普通核心子系统新增对 x86_64 entry frame、descriptor、selector 或 raw register layout 的直接依赖。

## 3. Fault Handling Scope

- [x] 3.1 保留 CPL/user-vs-kernel fault 分类，确保 CPL3 可恢复路径只覆盖现有 VMA-backed demand-zero、COW、stack-growth 或明确实现的有界用户恢复 case。
- [x] 3.2 对不可恢复用户 fault，确认路径会标记或终止当前进程并安排 safe teardown，而不是在 fault return path 直接释放 active user root、当前 kernel stack 或 process object。
- [x] 3.3 对 CPL0 或 kernel-owned diagnostic/syscall/IRQ/scheduler/teardown fault，确认仍进入现有 diagnostic 或 panic 边界，不误用用户 VMA recovery 逻辑恢复内核路径。

## 4. Documentation And Specs

- [x] 4.1 更新必要架构文档，说明 VM policy、address-space activation、user-entry 和 fault handling 的所有权边界；若修改 `docs/en`，同步对应 `docs/zh` 路径。
- [x] 4.2 如需调整 `roadmap.md`，仅保留项目规划级表述，不加入入口点、文件路径、命令、validation marker、源码实现细节或 archive/version index。
- [x] 4.3 更新或补充 source-adjacent notes/review notes，记录保留的低层 ABI 假设、未迁移的 x86_64 耦合点和后续拆分项。

## 5. Validation

- [x] 5.1 运行 `openspec status --change harden-vm-user-entry-boundary`，确认 proposal、design、specs 和 tasks 状态满足 apply 前要求。
- [x] 5.2 运行严格 OpenSpec 校验，验证 `vm-user-entry-boundary` 与 `architecture-core-boundary-discipline` delta 可解析且 requirement/scenario 格式正确。
- [x] 5.3 对 VM/user-entry/fault 边界执行 targeted consistency search，确认 boot/linker/page-table/vector/user ABI 假设未被静默移动或扩大，并确认核心层没有新增不必要的 x86_64 私有细节依赖。
- [x] 5.4 若实现修改 C++ source/header/assembly/build 配置，运行最窄可用 `xmake`/`x86_64-elf-gcc` cross-toolchain build；如 toolchain 缺失，记录缺失依赖、替代检查和剩余 runtime 风险。
- [x] 5.5 若实现修改 C++ source/header，运行贴近 freestanding C++17/x86_64/no-exceptions/no-RTTI 配置的 clang/clangd 辅助诊断，修复当前变更新引入的有效错误，并区分历史诊断、当前变更诊断和 freestanding 配置 false positive。
- [x] 5.6 若实现触及 user-entry、CR3/address-space activation、page fault、demand paging、COW、syscall return 或 userland runtime 行为，在环境支持时运行匹配的 QEMU headless smoke；如 QEMU、disk image、serial oracle 或本地配置不可用，记录原因和剩余 bootability/userland 风险。
- [x] 5.7 对早期 boot、page-table layout、port IO 或硬件行为风险较高的改动，考虑 Bochs 或 QEMU/Bochs cross-validation；如 Bochs 不可用，记录为手工/交叉验证剩余风险而非默认阻塞项。
