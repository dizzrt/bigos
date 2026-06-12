## 1. 构建边界和进程核心

- [x] 1.1 调整 `xmake.lua`，让 `kernel/core/proc` 的常规 lifecycle core 在非 `user_program_smoke`/`user_elf_smoke` 配置下可编译，同时保持 smoke entry 和 user image artifact 仍由显式开关控制
- [x] 1.2 拆分 `include/bigos/proc.h` 与 `kernel/core/proc` 中的 smoke-only entry、ELF loader、user-mode entry、process core 边界，保证 public header 只暴露最小 lifecycle API
- [x] 1.3 增加 `proc::init()` 或等价初始化路径，明确初始化顺序位于 memory、scheduler、syscall/user-mode prerequisites 之后，且不改变 boot/linker/page-table 地址常量
- [x] 1.4 将固定 PID 和单实例 `g_current_process`/`g_reap_pending_process` 替换为单核有界 PID allocator、process table、current process 指针和可迭代 reap/zombie tracking
- [x] 1.5 为 `Process` 增加 parent/child linkage、owned resource metadata、exit/fault status、zombie/reap 状态和 process-table publication/rollback 规则

## 2. exit、fault 和 wait 生命周期

- [x] 2.1 统一 `SYS_EXIT`、CPL3 fault、invalid user-buffer termination 的状态转换，确保当前 syscall/exception 栈和 active CR3/root 不在 unsafe path 被释放
- [x] 2.2 基于现有 blocking primitives 实现父进程 `wait` 所需的 wait queue 或等价阻塞机制，禁止 IRQ、preemption-disabled 和 scheduler critical section 内阻塞等待
- [x] 2.3 实现 child zombie 状态、parent waiter wakeup、status consumption、PID release 和 final reap 的顺序，避免重复 wakeup、重复回收或错误回收非子进程
- [x] 2.4 将 idle/reaper 或安全 kernel context 中的回收逻辑扩展为处理多个 reap-pending process，并保留 current-stack/current-root 安全检查
- [x] 2.5 更新 syscall ABI 或内部 syscall table，暴露最小 `wait`/`exit` 行为并记录错误返回、不可阻塞上下文和无可等待子进程的确定性结果

## 3. exec 和用户镜像加载

- [x] 3.1 将当前 bounded ELF64 loader 拆分为 passive validate/map/prepare 阶段和 commit 阶段，保证 commit 前失败能完整 rollback 新 image 资源
- [x] 3.2 实现 general `exec` primitive，复用 read-only exFAT/FS bounded read 和 ELF64 `ET_EXEC` 校验，不引入 fd/VFS、writable filesystem、dynamic linking 或 hosted file IO
- [x] 3.3 增加基础 `argv`/`envp` bounded copy 和初始用户栈布局，拒绝过多参数、过长字符串、越界 pointer table 或栈空间不足
- [x] 3.4 定义 exec commit 后失败策略，确保旧 image 不可恢复时通过 process lifecycle 受控终止，并向 parent wait 或诊断记录 deterministic exec failure status
- [x] 3.5 保持 `int 0x80`、GDT/TSS/RSP0、`iretq` ring3 entry、kernel higher-half、direct map、KVMEM 和 recursive self-map 地址/ABI 不变

## 4. smoke 迁移和文档

- [x] 4.1 迁移 embedded first-user-program smoke，使其复用 normal process lifecycle core，但仍保持 default-off、filesystem-independent 和独立 marker 行为
- [x] 4.2 迁移 `user_elf_smoke`，使 `/boot/user/init.elf` 加载路径复用 general exec/ELF prepare 逻辑，并保留 image packaging、64KiB bound 和 deterministic marker
- [x] 4.3 更新 runtime smoke matrix 或相关文档，记录 process lifecycle、wait/exit、exec argv/envp、safe reaper 和 smoke-only entry 的边界
- [x] 4.4 更新 `docs/en` canonical 文档并同步 `docs/zh` 对应路径，说明阶段 12 的进程生命周期能力、非目标和验证方法
- [x] 4.5 检查 `roadmap.md` 或阶段记录是否需要标注 `introduce-process-lifecycle` 的设计/实施状态，避免与已归档阶段 9-11 描述冲突

## 5. 源码检查和验证

- [x] 5.1 增加或更新 source-level checks，覆盖 PID uniqueness、process table capacity failure、parent/child linkage、wait wakeup、zombie-to-reap、exec rollback、argv/envp bounds、active-root teardown rejection 和 current-stack release deferral
- [x] 5.2 运行 `openspec validate introduce-process-lifecycle --strict`，修复当前 change 引入的 proposal/spec/tasks 格式或需求一致性问题
- [x] 5.3 运行最窄有用的 `xmake` cross-toolchain build；若 `x86_64-elf-gcc`/`x86_64-elf-g++`/`xmake` 不可用，记录 blocker、替代检查和残余风险
- [x] 5.4 对 C++ 源/头文件变更运行接近 freestanding C++17 x86_64 交叉环境的 clang/clangd 辅助静态检查；若工具或 flags 不可用，记录 gap、历史诊断和当前变更风险
- [x] 5.5 使用 QEMU headless serial-marker smoke 验证 user program、user ELF、wait/exit 或 exec marker；涉及 ring3/syscall/timer/blocking/ATA PIO 行为时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证
- [x] 5.6 将 validation notes 分离记录 passed checks、skipped checks 与原因、historical diagnostics、current-change diagnostics、emulator/toolchain blockers 和 residual bootability risk
