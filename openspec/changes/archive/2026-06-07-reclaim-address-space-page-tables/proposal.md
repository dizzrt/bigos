## Why

BigOS 已具备 direct map、用户地址空间派生和首个 ring3 用户程序 smoke，但用户地址空间退出后仍缺少明确 teardown 边界，动态页表页、用户物理页、用户 PML4 root 和进程内核栈容易长期滞留。现在补齐该能力，可以在继续推进 ELF 用户程序加载、多进程和更复杂用户态内存管理之前，先把页表 ownership、空页表回收和退出资源释放语义固定下来。

## What Changes

- 为动态创建的页表页建立可验证 ownership/level/present-entry 记录，区分 boot/kernel/self-mapping 依赖页表与可回收页表页。
- 扩展页表 unmap/free 路径，在叶子 PTE 清除后检查空 PT/PD/PDPT，并只回收当前 change 明确拥有的动态页表页。
- 新增用户地址空间 teardown helper，释放首个用户程序 smoke 拥有的用户 code/data/stack 物理页、用户 PML4 root、动态页表页和 process kernel stack。
- 明确 `SYS_EXIT`、用户态 `#PF` fault path 与未来多进程回收路径的责任边界，避免在当前用户栈、当前内核栈或错误 CR3 下释放仍在使用的资源。
- 为页表回收、用户进程退出资源释放和失败路径增加源码级、构建级和可选 Bochs marker 验证。

非目标：

- 不实现 SMP TLB shootdown、抢占调度、多进程公平性、`wait`/zombie 语义或完整进程树回收。
- 不实现 demand paging、copy-on-write、`mmap`、`brk`、自动用户栈增长或用户态 fault 恢复。
- 不实现 `fork`/`exec`、ELF 用户程序加载、文件系统加载用户程序或用户态 libc。
- 不回收 boot handoff、kernel image、direct map、KVMEM、recursive self-mapping 或其它静态内核运行所需页表。

## Capabilities

### New Capabilities

- `address-space-lifecycle`: 覆盖动态页表页 ownership、空页表页回收、用户地址空间 teardown、退出/故障后的延后释放边界和验证要求。

### Modified Capabilities

- `user-address-space-vmem`: 将已存在的 map/unmap primitive 扩展为可支持 owned 动态页表页追踪、叶子 PTE 清除后的空页表检查和当前 CPU TLB 失效边界。
- `first-user-program`: 将首个用户程序的 terminated 状态从“记录但不立即回收”扩展为可由安全 teardown 路径释放其用户地址空间、用户页和内核栈，同时保持当前执行路径不自毁。

## Impact

- 影响子系统：`src/mm` 页表 map/unmap、kernel vmem/free_pages、direct-map 辅助访问；`src/kernel/proc` 首个用户进程资源生命周期；`src/kernel/syscall` 的 `SYS_EXIT` 边界；用户态 `#PF` fault 终止路径。
- API/数据结构：新增或扩展地址空间生命周期 helper、页表页 ownership 元数据、用户映射记录和 process teardown 状态；保持公开内存分配 API 的页数/order 语义不变。
- 架构假设：x86_64 四级页表、单核、当前 CPU TLB invalidation 足够；无 SMP shootdown；syscall vector 仍为 `int 0x80`，exception/IRQ/syscall EOI 语义不变。
- 内存布局假设：boot 固定地址、higher-half kernel base、`KVMEM_BASE`、direct-map window、recursive self-mapping 地址和 BootInfo handoff ABI 不移动；direct map 可用于访问可回收页表页内容。
- 工具/验证假设：使用 xmake 与 `x86_64-elf-gcc`/`x86_64-elf-g++` 构建；源码级检查通过 `uv run pytest`；Bochs/serial oracle 可用时运行默认关闭的用户程序或内存 smoke，环境不可用时记录剩余 bootability 风险。
