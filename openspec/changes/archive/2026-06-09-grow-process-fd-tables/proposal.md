## Why

当前进程表与每进程 fd 表都是编译期固定大小的静态数组（`MAX_PROCESSES = 16`、`MAX_FDS = 16`），进程对象本身也以 `static Process` 单例形式分配（`init_process` / `first_process` / `smoke_process`），不存在真正的进程对象池与回收。路线图阶段 16 的 `fork` 会成倍创建进程并复制 fd 表，必然撞上这两个静态上限；阶段 15.5 要求在 `fork` 撞上静态槽位上限之前先移除它。趁现在语义面小、消费者少时把进程/fd 表改为可增长/可回收结构，是 `fork`/COW 的硬前置。

## What Changes

- 把进程表从固定大小静态数组 `g_process_table[MAX_PROCESSES]` 演进为可增长/可回收的进程注册结构，移除「最多 16 个进程」的编译期硬上限，改由可配置的软上限（默认值显著高于 16）约束。
- 引入真正的 `Process` 对象分配与回收：用 `kmalloc`/`free` 从内核堆分配进程对象，替代 `init_process` / `first_process` / `smoke_process` 等 `static Process` 单例，并在进程被 reap 后归还对象内存，配套确定性的分配失败语义。
- 把每进程 fd 表从内联固定数组 `FdEntry fd_table[MAX_FDS]` 演进为可增长结构，移除「每进程最多 16 个 fd」的编译期硬上限，改由可配置软上限约束；保持 `open`/`read`/`close`、`close_on_exec`、`close_all_fds` 的现有可观察语义不变。
- 把 PID 的分配/回收与新的可增长进程注册结构对齐：保持 PID != 0 且 != `WAIT_ANY`、`lookup_process` 的查找语义不变，并在进程对象回收时正确释放 PID 槽位，避免泄漏或复用错乱。
- 定义确定性失败语义：进程对象 / fd 表 / 注册结构的内核内存分配失败 -> 走既有进程创建失败 / `EAGAIN` / `EMFILE` 等返回路径，不 panic；达到软上限时返回确定性错误而非越界。
- 新增默认关闭的验证开关 `growable_tables_smoke`（`BIGOS_GROWABLE_TABLES_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_GROWABLE_TABLES_PASSED` / `BIGOS_GROWABLE_TABLES_FAILED`），覆盖「超过旧 16 上限仍能创建进程 / fd」「回收后槽位与 PID 可复用」「分配失败确定性降级」等路径。
- **非破坏性**：不改变 `int 0x80` ABI、IDT/向量布局、CR3 切换约定、用户低半区布局或进程生命周期状态机；不引入 `fork`/COW，仅移除其前置的静态上限。

## Capabilities

### New Capabilities
- `growable-process-fd-tables`: 进程注册结构与每进程 fd 表的可增长/可回收能力——堆分配的 `Process` 对象池与回收、可配置软上限替代编译期硬上限、PID 与槽位的分配/回收对齐、内存分配失败的确定性降级，以及默认关闭的验证开关。

### Modified Capabilities
- `process-lifecycle`: 进程对象不再以 `static Process` 单例与 `MAX_PROCESSES` 固定数组承载，改为堆分配 + 可增长注册 + reap 时回收；进程创建在分配失败或达到软上限时返回确定性错误。
- `fd-vfs-shell`: 每进程 fd 表不再是 `MAX_FDS` 固定内联数组，改为可增长结构；`install_fd_current` 在无法增长时返回确定性 `EMFILE`，其余 open/read/close/close_on_exec 语义不变。

## Impact

- 受影响子系统：`src/kernel/proc`（进程表、PID 分配、Process 对象分配/回收、fd 表）。
- 受影响代码：[proc.h](file:///Users/bytedance/Desktop/workspace/kernel/bigos/include/bigos/proc.h)（`MAX_PROCESSES` / `MAX_FDS` / `Process` / `FdEntry` 结构与相关声明）、[proc.cc](file:///Users/bytedance/Desktop/workspace/kernel/bigos/src/kernel/proc/proc.cc)（`g_process_table`、`alloc_pid`/`publish_process`/`unpublish_process`/`lookup_process`、`static Process` 单例创建点、`install_fd_current`/`read_fd_current`/`close_fd_current`/`close_all_fds`/`close_on_exec_fds`/`reap_pending_processes`）。
- 构建/验证：`xmake.lua` 新增 `growable_tables_smoke` 开关；QEMU headless serial-marker smoke 与源码契约/行为断言测试。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 不变；`kmalloc`/`free` 在进程创建/回收上下文（非 IRQ 上下文）可用；KTL 容器或等价手写结构在 freestanding 下可用；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：`fork`、COW、页引用计数、SMP/锁、per-CPU 进程状态、`MAX_VMAS` / `EXEC_MAX_ARGC` / `EXEC_MAX_ENVC` 等其余编译期上限的移除（按真实需求在后续阶段跟进）、信号、可写文件系统、用户态 libc。
