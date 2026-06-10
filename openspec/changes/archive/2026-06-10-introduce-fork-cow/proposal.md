## Why

当前进程只能通过 `create_elf_user_process` / `exec_current_from_elf_image` 这类「从镜像构造全新地址空间」的路径产生，没有任何「复制现有进程」的能力：没有 `fork`，也没有页引用计数与写时复制。路线图阶段 16 要求实现真正的进程复制，使 `fork`+`exec` 模型与未来 shell 成为可能；它依赖已完成的阶段 15（统一用户缺页处理）与阶段 15.5（可增长进程/fd 表）。趁现在语义面仍小（单核、同步、无信号）时把 `fork` 与 COW 一起立稳，是后续信号、可写文件系统与用户态 shell 的硬前置。

## What Changes

- 新增 `SYS_FORK`（`int 0x80` 新增一个固定 syscall 号，紧随现有 `SYS_MAP_ANON = 9` 之后），在父进程上下文复制出一个子进程：子进程获得父进程地址空间的 COW 副本、复制后的 fd 表、独立的 PID 与内核栈，父进程返回子 PID、子进程返回 0，并与现有 `wait`/`exit`/reaper 生命周期对齐。
- 引入 COW 地址空间复制：`fork` 时不逐页深拷贝用户物理页，而是为父子双方把可写匿名 backing 的用户 PTE 降权为只读并打上 COW 标记，共享底层物理帧；复制 VMA 元数据与物化记账，复制内核高半区借用项与每进程内核栈映射。
- 引入物理页引用计数：被 COW 共享的用户物理帧带引用计数，`fork` 时递增，写时分裂或地址空间 teardown 时递减，计数归零才真正归还 buddy；保证父子任一方先退出都不会过早释放共享帧。
- 把统一用户缺页处理（阶段 15 的 `try_handle_user_page_fault`）扩展出 COW 写错误分支：对只读且带 COW 标记的可写匿名页，写访问触发缺页时分配新帧、复制原页内容、以原 VMA 的可写权限重映射当前进程的该页，并递减原帧引用计数（计数为 1 时可原地恢复可写而不分配）。
- 复制每进程 fd 表：`fork` 后子进程获得父进程 fd 表的独立副本（共享底层 `vfs::File`，保持只读 VFS 语义不变），并保留 `close_on_exec` 标记位以配合后续 `exec`。
- 定义确定性失败语义：`fork` 过程中地址空间根派生、页表复制、fd 表复制、PID/进程对象分配等任一内核内存分配失败 -> 回滚已建立的部分状态并向父进程返回确定性负错误码（如 `-bigos::ENOMEM`/`-bigos::EAGAIN`），绝不 panic、绝不留下半复制的子进程；COW 写时分裂分配失败 -> 经既有 fault-to-lifecycle 路径确定性 kill 触发写错误的进程。
- 新增默认关闭的验证开关 `fork_cow_smoke`（`BIGOS_FORK_COW_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_FORK_COW_PASSED` / `BIGOS_FORK_COW_FAILED`），覆盖「fork 后父子可独立运行」「写时分裂使父子内存隔离」「引用计数在父子先后退出时正确归还」「分配失败确定性降级」等路径；保留现有 `demand_paging_smoke` / `user_*_smoke` / `growable_tables_smoke` 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` 寄存器 ABI 约定（仅新增一个 syscall 号）、IDT/向量布局、页表自映射地址、CR3 切换约定、higher-half/direct-map/`KVMEM_BASE` 布局或用户低半区布局；`#PF` 仍是异常路径、不发 EOI；不引入 SMP/锁、信号、可写文件系统或用户态 libc。

## Capabilities

### New Capabilities
- `fork-copy-on-write`: 进程复制与写时复制能力——`SYS_FORK` 父子语义、COW 地址空间复制、用户物理帧引用计数、缺页写时分裂、fd 表复制，以及内核内存分配失败/写分裂失败的确定性降级与默认关闭验证开关。

### Modified Capabilities
- `process-lifecycle`: 进程不再只能从 ELF 镜像构造，新增「从当前进程复制」的创建路径；`fork` 产生的子进程纳入既有父子链接、`wait`/`exit`、zombie/reaper teardown 语义，并在复制失败时返回确定性错误而非创建半成品进程。
- `syscall-entry`: `int 0x80` ABI 新增 `SYS_FORK` 号，父进程返回子 PID、子进程返回 0、失败返回负 errno；其余 syscall 号、寄存器约定与「syscall 不发 EOI」不变。
- `demand-paging`: 统一用户缺页处理新增 COW 写错误分支——对带 present 位的可写匿名只读页（COW 共享）执行写时分裂或原地恢复可写，区别于现有「present 位即视为权限违例 kill」的判定。
- `vma-user-memory-api`: VMA 集合与物化记账新增「按当前地址空间被复制」的语义；`brk`/匿名映射建立的可写匿名区间在 `fork` 时进入 COW 共享而非深拷贝。

## Impact

- 受影响子系统：`src/kernel/proc`（`fork` 实现、地址空间复制、fd 表复制、引用计数、COW 写时分裂、进程生命周期）、`src/kernel/syscall`（`SYS_FORK` 分发）、`src/kernel/irq`（`page_fault_handler` 经 `try_handle_user_page_fault` 的 COW 分支）、`src/mm`（用户物理帧引用计数、按根复制页表的支持函数）。
- 受影响代码：[syscall.h](file:///Users/bytedance/Desktop/workspace/kernel/bigos/include/bigos/syscall.h)（新增 `SYS_FORK`）、[syscall.cc](file:///Users/bytedance/Desktop/workspace/kernel/bigos/src/kernel/syscall/syscall.cc)（dispatch 分支）、[proc.h](file:///Users/bytedance/Desktop/workspace/kernel/bigos/include/bigos/proc.h)（`fork_current` 声明、COW/引用计数相关结构与 `try_handle_user_page_fault` 扩展）、[proc.cc](file:///Users/bytedance/Desktop/workspace/kernel/bigos/src/kernel/proc/proc.cc)（地址空间/VMA/fd 复制、COW 缺页分支、确定性回滚）、[memory.h](file:///Users/bytedance/Desktop/workspace/kernel/bigos/include/bigos/memory.h) 与 `src/mm`（用户物理帧引用计数与按根复制页表的辅助 API）。
- 构建/验证：`xmake.lua` 新增 `fork_cow_smoke` 开关；QEMU headless serial-marker smoke 与源码契约/行为断言测试（沿用阶段 14.5 启动的行为断言测试轨道）。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 不变；`#PF` 的 CR2 取缺页地址、`error_code` 位含义（present/write/user）不变；`kmalloc`/`free` 在 `fork`（非 IRQ）上下文可用；COW 缺页发生在可安全分配的 CPL3 上下文；阶段 15.5 的可增长进程/fd 表与阶段 15 的统一缺页处理已就位；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：`exec` 后释放 COW（沿用既有 teardown 即可，不在本阶段优化）、file-backed mmap、shared/MAP_SHARED 映射、swap、page cache、`vfork`/`clone` 标志位、写时复制下的多页预取/超页、SMP 与 per-CPU/TLB shootdown 下的引用计数并发、信号交付、墙钟/uid-gid、可写文件系统、用户态 libc/shell。这些留给阶段 16.5/17/18/19 与并行轨道。
