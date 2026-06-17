## Context

BigOS 当前进程只能由 ELF 镜像构造地址空间（`create_elf_user_process` / `exec_current_from_elf_image`），用户内存通过demand paging capability 的统一缺页处理 `try_handle_user_page_fault` 惰性物化匿名页。已具备的前置：

- growable process and fd table capability：进程注册结构与每进程 fd 表已是堆分配、可增长/可回收（`MAX_PROCESSES_SOFT_LIMIT` / `MAX_FDS_SOFT_LIMIT`），`Process` 对象经 `alloc_process_object` 分配、reap 时回收。
- demand paging capability：`try_handle_user_page_fault` 对 `error_code` present 位（bit0）即判定为保护违例并返回 false（交由调用方 kill）；匿名页惰性物化已用 `alloc_user_frame`/`free_user_frame`/`map_user_page_for_process` 与 VMA `materialized_*` 记账。
- 地址空间：`derive_user_address_space_root()` 复制内核高半区顶层项、零填充低半区；`teardown_user_address_space()` 仅回收进程拥有的低半区页表项并最后释放 PML4；`map_page_in_root()` 按显式 root 建立映射且不切 CR3。

fork/exec process capability 要在此之上引入 `fork` 与 COW：复制一个进程而非从镜像构造，并以写时复制避免逐页深拷贝。约束：x86_64 单核、同步、无信号、`int 0x80` 与 `InterruptFrame` ABI 不变、`#PF` 仍是异常路径不发 EOI、不引入 SMP/锁。

## Goals / Non-Goals

**Goals:**

- 新增 `SYS_FORK`（`int 0x80`，号紧随 `SYS_MAP_ANON = 9`），父返回子 PID、子返回 0、失败返回负 errno。
- COW 复制父地址空间：可写匿名 backing 的已物化页在父子双方降权只读 + COW 标记、共享物理帧；只读/可执行页直接共享；未物化惰性区间仅复制元数据。
- 用户物理帧引用计数：fork 共享时 +1，写分裂/teardown 时 -1，归零才还 buddy，保证父子退出顺序无关。
- 缺页处理新增 COW 写分裂分支：写 COW 页 -> 分配新帧+复制内容+重映射可写并 -1 原帧；原帧计数为 1 时原地恢复可写。
- 复制 fd 表：子进程独立 fd 槽副本，共享底层只读 `vfs::File`，保留 `close_on_exec`。
- 确定性失败与回滚：fork 任一分配失败回滚部分子状态、父进程返回负 errno 存活；写分裂分配失败经 fault-to-lifecycle kill。
- 默认关闭 `fork_cow_smoke` 验证。

**Non-Goals:**

- `vfork`/`clone` 标志、`exec` 后 COW 优化（沿用既有 teardown）、file-backed/MAP_SHARED 映射、swap、page cache。
- SMP / per-CPU / TLB shootdown 下的并发引用计数（本阶段引用计数仅在单核非 IRQ 上下文更新）。
- 信号交付、墙钟/uid-gid、可写文件系统、用户态 libc/shell。

## Decisions

### 决策 1：fork 在内核线程/syscall 上下文同步执行，而非复制陷阱帧寄存器栈

`SYS_FORK` 由 dispatch 在父 syscall 上下文调用 `proc::fork_current()`。子进程的用户态返回值（0）通过把父 `InterruptFrame` 拷贝进子进程内核栈、并改写子帧的 `rax=0` 实现；父进程的 `rax` 由 dispatcher 写为子 PID。子进程首次被调度时经现有 `iretq` ring3 路径用其内核栈上的帧返回用户态。

理由：复用既有「内核栈顶保存 `InterruptFrame` + `iretq` 进 ring3」机制（`run_user_process` / 上下文切换帧），不需要新的进入路径。备选（在 dispatch 里直接构造一个 ELF 风格的全新入口）无法保留父用户寄存器状态，违反 fork 语义。

### 决策 2：COW 复制以「遍历父 VMA + 按页处理 PTE」实现，而非通用页表深拷贝

新增 `clone_user_address_space_cow(parent, child)`：
1. `child->address_space_root = derive_user_address_space_root()`（复制内核高半区借用项）。
2. `clone_process_kernel_stack_mapping(child)` 建立子独立内核栈映射（沿用现有函数）。
3. 复制 `VmaCollection`（值复制，含 `materialized_*`、heap/anon 记账）。
4. 遍历每个 VMA 的已物化页区间，按 backing 与可写性分流（细则见决策 8、9）：
   - `VmaBacking::ElfSegment` 页（Code / 只读 Data / 可写 Data）：为子进程分配新帧、复制父帧内容、按段属性独立映射进子 root；不纳入引用计数、不打 COW 标记。
   - `VmaBacking::Anonymous` 且可写已物化页：把父子两侧 PTE 都重映射为「present|user|NX、清 WRITABLE、置 `PTE_COW`」，共享父帧，匿名帧引用计数 +1。
   - `VmaBacking::Anonymous` 且只读已物化页：直接把父帧 phys 以只读属性映射进子 root，匿名帧引用计数 +1（只读页不会变可写，无需 COW 标记）。
   - guard 页与内核高半区借用项：跳过，不复制叶子帧、不计数。

理由：BigOS 没有现成的「按 root 深拷贝整张页表」原语，且 VMA + `materialized_*` 已精确描述哪些页真实存在，按 VMA 遍历比盲扫页表更省、更可控。需要新增 mm 辅助：`remap_user_page_in_root(root, vaddr, phys, attr)`（覆盖已存在 PTE 属性，含 invlpg）与读取父 PTE phys 的访问器。

### 决策 3：COW 标记复用页表项中一个被忽略的可用位

x86_64 PTE 有 3 个软件可用位（bit 9-11）。选 bit9 作为 COW 标记（`PTE_COW`），与 present/writable/user/NX 等硬件位正交。缺页时通过查父/子 PTE 是否带 `PTE_COW` 且 WRITABLE=0 判定 COW 页。

理由：无需额外 side table，单核下读改写 PTE 即可。备选（用独立 bitmap 跟踪 COW 页）增加同步与内存成本，单核不必要。

### 决策 4：物理帧引用计数用直接映射区一张定长数组，按帧号索引，在 `init_direct_map` 之后初始化

新增 mm 内部 `frame_refcount`：以「物理帧号 = phys >> PAGE_SHIFT」为索引的定长 `uint16_t` 计数数组。承载位置与初始化时机已收敛（前为 Open Question）：

- 现有内存初始化顺序为 `init_cache -> init_buddy -> init_vmem -> init_direct_map`（见 `kmem.cc::init_mem`），用户帧来自 buddy `alloc_physical_order(0, 0)`。
- 计数表覆盖范围按「最高可分配物理帧号」定界（由 boot 内存图/buddy 已知的物理上界推导），表大小 = `(max_frame + 1) * sizeof(uint16_t)`，数十 MB RAM 下为数十~上百 KB。
- 承载位置：直接映射区的一段定长存储，在 `init_direct_map` 完成后、首个用户进程创建前一次性建立（新增 `init_frame_refcount()` 步骤接在 `init_mem` 末尾）。选直接映射定长数组而非 `kmalloc` 一次性分配，是因为它在 slab 之外、O(1) 物理帧号寻址、且不参与 slab/回收抖动。

提供 `frame_ref_inc(phys)` / `frame_ref_dec_and_maybe_free(phys)`。`alloc_user_frame` 返回的新帧初始计数视为 1（单一所有者）；fork 共享时 +1；teardown 与写分裂调用 dec，归零才 `free_user_frame`。

理由：单核、帧数有界，定长数组 O(1) 且无分配抖动；避免把计数塞进 `page` 元数据结构带来的跨模块改动。备选（slab 节点链表）复杂且 IRQ 安全性更难保证；本阶段引用计数只在非 IRQ 上下文更新，无需原子操作，但接口需在注释中标注「单核/非 IRQ-context only」。

### 决策 5：teardown 走引用计数感知路径

现有 `teardown_user_address_space(root)` 直接释放低半区拥有的帧。fork 引入共享后，teardown 对每个进程拥有的用户叶子帧统一调用 `frame_ref_dec_and_maybe_free` 而非无条件 `free`。统一化的依据是「`alloc_user_frame` 返回帧时初始计数即为 1」：

- 普通（非 fork）进程、ELF 段独立副本帧：计数恒为 1，dec 后归零正常释放，与原 free 语义逐位等价。
- COW 共享的匿名帧：fork 时已 +1，计数 >1 时 dec 只递减、把帧留给另一方，归零才释放。
- guard 页与内核高半区借用项：teardown 按现有「借用不释放」规则跳过，不进入 dec。

理由：让「无条件释放」与「COW 共享释放」收敛到同一引用计数路径，避免两套 teardown，也免去 teardown 反查 backing。约束：所有进入 dec 的叶子帧都必须曾经过 `alloc_user_frame`（计数被初始化为 1），否则会对未初始化计数的帧 dec —— 因此本阶段把全部用户叶子帧（ELF 段帧与匿名帧）统一纳入计数表，guard/borrowed 不计数（见决策 9 与风险）。

### 决策 6：COW 缺页分支放在 `try_handle_user_page_fault` 现有 present 判定之前

当前逻辑：present 位（bit0）置位即 `return false`（kill）。改为：present 位置位时，先查覆盖页的 PTE 是否带 `PTE_COW`、WRITABLE=0、且 VMA 为可写匿名 backing 且本次为写访问（bit1）；满足则进入 `cow_split_current(page, vma)`；否则维持原 `return false`（真正的保护违例仍 kill）。

控制流：
```
#PF -> page_fault_handler (irq) -> try_handle_user_page_fault
  present==0: 既有 demand-zero 物化（不变）
  present==1 且 write 且 PTE_COW 且 VMA 可写匿名:
      ref == 1 -> 原地置 WRITABLE、清 PTE_COW、invlpg -> resume
      ref >  1 -> 分配新帧失败? -> return false(kill)
                  成功 -> 复制原帧内容 -> remap 子页 writable -> 原帧 ref-- -> resume
  其它 present==1: return false (kill，语义不变)
```

### 决策 7：fork fd 表复制复用 grow 路径

子进程 `fd_table` 先按父 `fd_capacity` 用 `kmalloc` 分配等容数组，逐项值复制 `FdEntry`，对每个非空 `file` 调用 `vfs` 的引用获取（与 `open` 安装一致的 retain 语义）以保持 `vfs::File` 引用计数正确，使父子各自 `close` 互不影响。

理由：与既有 `install_fd_current`/`close_fd_current` 的 retain/release 语义对齐，避免一方 close 释放另一方仍用的 `File`。

### 决策 8：引用计数表承载与初始化（前 Open Question 收敛）

- 决定：引用计数表为直接映射区一段定长 `uint16_t` 数组，按物理帧号索引，在 `init_mem` 末尾、`init_direct_map` 之后新增 `init_frame_refcount()` 一次性建立；上界取 boot 内存图/buddy 已知的最高可分配物理帧号。
- 不选「`kmalloc` 一次性分配」：计数表本身是 mm 基础设施，应独立于 slab 生命周期，且首个用户进程创建早于任何大规模 slab 使用，定长直接映射存储更稳定。
- 计数饱和（达到 `uint16_t` 上限）按确定性失败处理：`frame_ref_inc` 在饱和时让对应 `fork` 步骤失败回滚，而不是溢出回绕。

### 决策 9：ELF 段不参与 COW，按独立副本复制（前 Open Question 收敛）

- 决定：本阶段只对 `VmaBacking::Anonymous` 页做 COW 共享；`VmaBacking::ElfSegment` 页（含可写 `.data`、只读 `.rodata`、可执行 `.text`）在 `fork` 时为子进程分配新帧并复制内容、按段属性独立映射，不共享、不打 `PTE_COW`。
- 理由：当前 ELF 加载器的段是 eager 映射且有界（受 `USER_ELF_MAX_FILE_BYTES` 等约束），段页数量小，独立复制成本可接受；这样 COW 写分裂分支只需处理「可写匿名页」一种情形，缺页判定与回滚显著简化，降低首版风险。
- 代价：`fork` 时 ELF 段不享受 COW 的「零拷贝」收益。可在后续阶段（如 `exec` 优化或只读段共享）再把只读 ELF 段纳入引用计数共享；本阶段为非目标。
- 与决策 5 的一致性：ELF 段副本帧同样经 `alloc_user_frame`（初始计数 1），teardown 走统一 dec 路径，无需特例。

## Risks / Trade-offs

- [引用计数表内存开销] 按最高物理帧号建表，几十 MB 物理内存下为数十~上百 KB。→ 用 `uint16_t` 定长数组覆盖到最高可分配帧号；计数饱和按确定性失败（回滚 `fork`）而非回绕。
- [借用/guard 帧误进入计数导致重复释放或泄漏] 内核高半区借用项与 guard 页不应进入用户帧引用计数。→ 仅对经 `alloc_user_frame` 的用户叶子帧（ELF 段独立副本 + 匿名页，初始计数 1）计数；guard/borrowed 高半区项 teardown 时按现有「借用不释放」规则跳过、不 dec；新增源码契约断言区分（决策 5、9）。
- [COW 标记位与未来硬件/软件位冲突] bit9 若日后被其他特性占用会冲突。→ 在 `memory.h` 集中定义 `PTE_COW` 常量并注释占用，源码契约断言其唯一性。
- [写分裂时 TLB 失效遗漏导致读到旧帧] 重映射后必须 invlpg 当前页。→ `remap_user_page_in_root` 内置 invlpg；单核无需跨核 shootdown，注释标注 SMP 时需补 shootdown。
- [fork 中途失败回滚不彻底导致帧/PID 泄漏] 复制跨多步。→ 以「逐步登记、失败逆序回滚」实现：已 ref++ 的帧在回滚时 ref--，已 derive 的 root 调 teardown，已分配 fd_table/Process 释放，PID 归还；smoke 覆盖分配失败注入。
- [引用计数在 IRQ 上下文被并发更新] 缺页发生在异常上下文。→ 约定缺页处理在已开中断且可分配的 CPL3 上下文执行，引用计数更新点（fork/split/teardown）均非 IRQ handler；接口注释标注「单核非 IRQ-context only」，与现有 mm 原语一致。

## Migration Plan

1. mm 层：新增 `PTE_COW` 常量、`frame_refcount` 直接映射定长表与 `init_frame_refcount()`（接在 `init_mem` 末尾、`init_direct_map` 之后）、`frame_ref_inc/dec`、`remap_user_page_in_root` 与父 PTE phys 访问器；`teardown_user_address_space` 改走引用计数 dec。先以非 fork 进程验证「初始计数 1、teardown 正常释放」不回归（demand_paging/user smoke）。
2. proc 层：新增 `fork_current()`、`clone_user_address_space_cow()`、`clone_fd_table()`、`cow_split_current()`；`try_handle_user_page_fault` 加 COW 分支；`proc.h` 增声明。
3. syscall 层：`syscall.h` 增 `SYS_FORK`，`syscall.cc` dispatch 增分支（非 IRQ/可分配上下文校验）。
4. 验证：`xmake.lua` 增 `fork_cow_smoke`；新增 `fork_cow_smoke_entry` 与行为断言/源码契约测试。
5. 回滚策略：所有新增能力在 `BIGOS_FORK_COW_SMOKE` 与 `SYS_FORK` 分支后，普通 boot 不调用 `fork`；引用计数路径对非 fork 进程等价于原 free 语义，可独立验证不回归后再接入。

## Open Questions

- 暂无。两项原 Open Question 已收敛为决策 8（引用计数表承载于直接映射区定长数组、`init_direct_map` 后初始化）与决策 9（ELF 段不参与 COW、按独立副本复制，仅匿名页 COW 共享）。实现阶段如发现 `init_frame_refcount` 与现有 `init_mem` 顺序存在隐藏依赖，再回到决策 8 复核。
