## Context

当前进程模型把所有进程承载在编译期固定数组与单例上：

- 进程注册表：`bigos::proc::Process *g_process_table[MAX_PROCESSES]`（`MAX_PROCESSES = 16`），`publish_process` 线性扫描首个空槽，`lookup_process` 线性按 PID 查找，`unpublish_process` 线性清槽并维护父子链。
- 进程对象本体：`launch_init` 用 `static Process init_process`、`user_program_smoke_entry` 用 `static Process first_process`、demand-paging smoke 用 `static Process smoke_process`，没有进程对象池，也没有按 PID 创建任意数量进程的能力。
- 每进程 fd 表：`FdEntry fd_table[MAX_FDS]`（`MAX_FDS = 16`）内联在 `Process` 里，`install_fd_current` 线性扫描首个空位，`read/close/close_all/close_on_exec` 按下标线性遍历。
- PID 分配：`g_next_pid` 自增并跳过 `0` / `WAIT_ANY`，`alloc_pid` 用 `lookup_process` 去重。

fork/exec process capability 的 `fork` 会在单次调用内复制一个进程及其 fd 表，并支撑多代进程并存，`MAX_PROCESSES = 16` / `MAX_FDS = 16` 与 `static Process` 单例都会成为硬墙。growable process and fd table capability 的目标是「在 `fork` 撞上之前移除静态槽位上限」，但不引入 `fork` 本身。

约束：freestanding、单核、同步、无 libc、无异常/RTTI；`kmalloc`/`free` 已可用，但**不可**在 IRQ 上下文做分配；进程创建/回收发生在普通内核线程上下文（`can_block()` 为真）。

## Goals / Non-Goals

**Goals:**
- 移除 `MAX_PROCESSES` / `MAX_FDS` 这两个会被 `fork` 撞上的编译期硬上限，改为可增长/可回收结构 + 可配置软上限。
- 用 `kmalloc`/`free` 提供真正的 `Process` 对象分配与回收，替代 `static Process` 单例。
- 保持 PID 分配/回收、`lookup_process`、父子链、reap 的现有可观察语义不变。
- 保持 fd 的 open/read/close/close_on_exec/close_all 可观察语义不变。
- 内存分配失败与软上限达到时确定性降级（返回错误），不 panic。
- 提供默认关闭的验证开关与固定 marker。

**Non-Goals:**
- 不实现 `fork`、COW、页引用计数。
- 不移除 `MAX_VMAS` / `EXEC_MAX_ARGC` / `EXEC_MAX_ENVC` 等其他编译期上限（留待后续按真实需求）。
- 不引入锁 / per-CPU / SMP；结构按单核非并发设计。
- 不改变 `int 0x80` ABI、IDT/向量、CR3 切换、用户低半区布局或进程状态机。

## Decisions

### 决策 1：进程注册表用侵入式链表 + 软上限，而非更大的固定数组
- 方案：把 `g_process_table[MAX_PROCESSES]` 改为侵入式双向链表——在 `Process` 内嵌注册节点指针（`reg_next` / `reg_prev`，或内嵌一个 `ktl::__detail::_List_node_base`），由全局链表头穿起所有已注册进程。注册/注销为 O(1) 链表操作、无元素搬迁、不额外分配节点内存（节点随 `Process` 对象一起分配）。以 `constexpr uint32_t MAX_PROCESSES_SOFT_LIMIT = 1024` 作为软上限：`publish_process` 在当前活跃进程数未达软上限时挂入链表，达到软上限返回失败。
- 备选 1：直接把 `MAX_PROCESSES` 调大到 256/1024 仍用固定数组。否决：仍是硬上限，且静态占用大，未解决「`fork` 撞上限」的根因，也不符合growable process and fd table capability「可增长/可回收」的明确措辞。
- 备选 2：KTL `ktl::list<Process*>` 动态数组/链表容器。否决：`ktl::list` 是 std::list 风格的「拥有式」容器，每次插入会额外 `kmalloc` 一个节点，引入与 `Process` 对象分离的第二条失败面与生命周期；侵入式节点随对象分配，零额外分配、与 reap 链/父子链同寿命，更契合单核内核语义。
- 理由：可增长结构把上限从「编译期硬墙」变成「资源/策略软上限」，回收后链表节点随对象自然摘除复用；侵入式设计对 reap 链友好且无搬迁。

### 决策 2：`Process` 对象用 `kmalloc`/`free` 堆分配，引入对象生命周期
- 方案：新增内部 `Process *alloc_process_object()`（`kmalloc(sizeof(Process))` + `memset` 清零）与 `void free_process_object(Process*)`（`free`）。`launch_init` / smoke 入口从 `static Process` 改为堆分配。`reap_pending_processes` 在 `unpublish_process` 之后、确认资源已回收（`resources_reclaimed`）时释放对象内存。
- 关键时序：必须在进程不再被任何指针引用（`g_current_process`、reap 链、父子链）后才能 `free`，否则悬垂。设计上在 reap 流程末尾、清空 reap 链节点并 `unpublish_process` 后释放。
- 备选：保留 slab 风格的固定对象池。否决：又引入新的固定上限，与目标冲突；`kmalloc` 已满足需求且失败语义清晰。
- 失败行为：`alloc_process_object` 返回 `nullptr` -> 进程创建路径返回确定性失败（沿用现有 `create_*` 失败返回 / syscall `EAGAIN` 语义），不 panic。

### 决策 3：fd 表改为可增长结构，软上限替代 `MAX_FDS`
- 方案：把 `Process::fd_table[MAX_FDS]` 改为可增长的 fd 存储（堆分配的 `FdEntry` 动态数组，随进程对象生命周期分配，进程回收时 `free`），以 `constexpr uint32_t MAX_FDS_SOFT_LIMIT = 256` 约束。`install_fd_current` 复用空位或增长；达到软上限返回 `-EMFILE`。`read/close/close_all/close_on_exec` 改为按当前容量遍历。
- 不变量：fd 号仍是「当前表内最小空闲下标」语义（保持 `open` 返回值可预测，兼容现有测试期望）。
- 失败行为：fd 表增长分配失败 -> `install_fd_current` 返回 `-EMFILE`（与达到上限同义，确定性、不 panic）。

### 决策 4：PID 分配/回收与新结构对齐
- 方案：保持 `g_next_pid` 自增 + 跳过 `0`/`WAIT_ANY` + `lookup_process` 去重的现有算法不变，只是 `lookup_process` / `publish` / `unpublish` 改为遍历新容器。`alloc_pid` 的循环上界从 `MAX_PROCESSES * 2` 改为基于当前活跃进程数 / 软上限的等价界，避免在大量进程下提前放弃。
- 理由：PID 算法本身正确，问题只在它依赖固定数组遍历；最小改动对齐即可。

### 决策 5：验证开关 `growable_tables_smoke`
- 方案：`xmake.lua` 新增默认关闭开关 `growable_tables_smoke` -> `BIGOS_GROWABLE_TABLES_SMOKE`。smoke 从普通内核线程上下文运行，构造并注册「超过旧 16 上限」数量的进程对象与 fd，验证：(a) 第 17+ 个进程/fd 仍能创建；(b) 回收后槽位与 PID 可复用；(c) 注入分配失败时确定性降级。通过/失败发射 `BIGOS_GROWABLE_TABLES_PASSED` / `BIGOS_GROWABLE_TABLES_FAILED`。保留现有 `user_*_smoke` / `demand_paging_smoke` 矩阵不删除。

## Risks / Trade-offs

- [`Process` 对象释放时机错误导致悬垂指针] → 仅在 reap 末尾、`unpublish_process` 且无引用后 `free`；保留 `resources_reclaimed` / `table_published` 守卫；smoke 覆盖回收后复用路径。
- [`kmalloc` 在错误上下文被调用（IRQ）] → 约束所有进程/fd 分配只在进程创建/回收（非 IRQ、`can_block()`）路径发生；缺页/IRQ 路径不分配进程对象或 fd 表。
- [堆分配引入新的失败面（原静态数组永不失败）] → 所有新分配点定义确定性返回错误（创建失败 / `EAGAIN` / `EMFILE`），并在 smoke 中注入失败验证降级，绝不 panic。
- [软上限默认值过大占用内存 / 过小限制 `fork`] → 选定进程软上限 1024、fd 软上限 256，明显高于 16 但有界，以 `constexpr` 集中可调；后续阶段按真实需求调整。
- [行为契约测试断言旧的固定数组源码字符串] → 同步更新受影响的源码契约测试，并按路线图「行为断言测试」方向尽量改为 marker/行为断言。

## Migration Plan

1. 在 `proc.h` 引入软上限常量、调整 `Process` 的 fd 存储字段与 `FdEntry`，保留对外 API 签名不变。
2. 在 `proc.cc` 实现进程注册容器、`alloc/free_process_object`、fd 表增长/回收，逐个替换 `static Process` 单例与 `MAX_PROCESSES`/`MAX_FDS` 遍历。
3. 替换 `launch_init` 与各 smoke 入口的进程对象来源。
4. 加 `growable_tables_smoke` 开关与 marker。
5. 更新源码契约测试与文档（docs/en + docs/zh 镜像）。
- 回滚：本变更自成一体，可整体回退到固定数组实现；不触碰 ABI / 引导 / 页表，回滚无残留风险。

## Open Questions

- 已决：进程软上限取 `MAX_PROCESSES_SOFT_LIMIT = 1024`、fd 软上限取 `MAX_FDS_SOFT_LIMIT = 256`，以 `constexpr` 集中定义，后续阶段按真实需求调整。
- 已决：进程注册容器采用侵入式双向链表（注册节点内嵌于 `Process`，随对象一起分配/释放），不使用拥有式 `ktl::list`；理由见决策 1。
