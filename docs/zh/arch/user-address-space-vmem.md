# 用户地址空间页表准备

本阶段把 `kernel/mm/vmem.cc` 中“仅服务内核范围”的页表代码抽象成显式的 map/unmap
primitive，并定义 user/kernel 页属性策略与用户地址空间页表根的最小派生。默认 helper 仍不隐式切换
CR3；默认关闭的首个用户程序运行路径会显式激活派生根并进入 ring3。

## 显式页属性 primitive

新增 `bigos::mm::PageAttr` 与 `page_attr` 常量（声明于 `include/bigos/memory.h`），bit
位与 x86_64 paging-structure 条目对齐，可直接 OR 到物理帧地址上：

| 名称 | bit | 含义 |
| --- | --- | --- |
| `PRESENT` | 0 | 条目有效 |
| `WRITABLE` | 1 | 可写 |
| `USER` | 2 | 用户态可访问 |
| `GLOBAL` | 8 | 全局页 |
| `NO_EXECUTE` | 63 | 不可执行（依赖 EFER.NXE） |

核心 primitive：

- `bool map_page(uint64_t vaddr, uint64_t phys, PageAttr attr)`：建立单个 4 KiB 映射，
  复用现有递归 self-mapping 遍历与缺级页表分配，缺级失败时回滚已创建的中间级并返回
  `false`。当 `attr` 是 user 映射时，中间级条目继承 user bit 以保证叶子页可达；NX 只在叶子
  PTE 上编码。
- `void unmap_page(uint64_t vaddr)`：清除 PTE 并对该地址执行 `invlpg`，与现有
  `rollback_kernel_range()` 的失效语义一致。
- `bool map_page_in_root(uint64_t root, uint64_t vaddr, uint64_t phys, PageAttr attr)`：不写 CR3，
  通过 direct map 修改指定派生根的低半区页表，供用户程序 loader 在内核地址空间仍处于活动状态时
  填充 code/data/BSS/stack。
- `bool user_range_mapped(uint64_t root, uint64_t vaddr, uint64_t len)`：验证 bounded 用户范围低于
  canonical user half，且每页 PTE present/user；本阶段不实现 demand paging，后续
  process/VMA 代码补充 bounded demand-zero 与 COW fault 处理。

两者均为 non-interrupt-context-only，写入条目时使用 `InterruptGuard` 屏蔽同 CPU maskable
IRQ 交错，不得从 IRQ handler 调用，也不在 IRQ handler 中触发动态分配。

## user / kernel 属性策略

| 用途 | 常量 | bit 组合 | 与旧 `0x3` 关系 |
| --- | --- | --- | --- |
| 内核默认 | `KERNEL_DEFAULT` | `PRESENT | WRITABLE` | bit-for-bit 等价旧 `DEFAULT_ATTR_PTE = 0x3`（user=0、NX=0） |
| 用户数据页 | `USER_DATA` | `PRESENT | WRITABLE | USER | NO_EXECUTE` | 置 user bit、NX 编码 |
| 用户代码页 | `USER_CODE` | `PRESENT | USER` | 置 user bit、清 NX |

`VMem::map_kernel_range()` / `unmap_kernel_range()` 现在经由 `map_single_page()` 以
`KERNEL_DEFAULT` 表达，PTE bit 行为与旧 `0x3` 完全一致，内核范围不会被置 user bit。源码级
测试断言该等价关系。

## EFER.NXE 状态与 NX 降级

当前 long-mode 进入路径 `kernel/arch/x86/boot/boot.s` 在 `IA32_EFER`（MSR `0xc0000080`）只置
`LME`（bit 8），**未使能 NXE（bit 11）**。因此 NX bit 当前主要作为“属性编码正确”验证，不能
依赖运行时强制不可执行；首个用户程序仍会把数据/BSS/stack 映射为 `USER_DATA`，但 runtime NX 强制
留待后续 enable NXE change。

**剩余风险**：在 NXE 未使能时，若未来代码错误地依赖 NX 运行时保护，将得不到硬件强制；本阶段以
源码级编码检查覆盖该差距并显式记录。

## 用户地址空间页表根派生

`uint64_t derive_user_address_space_root()`：

- 分配一页新 PML4，通过 direct map 访问它。
- **复制内核 PML4 高半区顶层条目**（index 256..511，覆盖 kernel higher-half、self-mapping、
  direct map、KVMEM），使内核地址在派生根中共享。
- **清零低半区条目**（index 0..255），保证用户区相互独立。
- 返回新根物理地址，失败返回 `INVALID_PHYS_ADDR`。

**self-mapping 语义**：派生根共享内核高半区，self-mapping 槽位仍来自内核 PML4；首个用户程序
运行路径只依赖高半区/direct map/KVMEM 可达性和 root-targeted mapper，不在用户根激活后修改低半区
页表。

## 受控激活边界

`read_cr3()` / `activate_address_space_root(root)` 是显式 API，只在 `proc::run_user_process()` 使用：

- loader 先通过 `derive_user_address_space_root()` 和 `map_page_in_root()` 构建低半区映射。
- 进入 ring3 前记录当前 kernel root、设置 TSS/RSP0，然后写 CR3 激活用户 root。
- `SYS_EXIT` 或受控用户 fault 会记录进程状态，并在不立即释放当前栈/进程对象的前提下恢复 kernel root。
- 普通派生 helper 不写 CR3、不进入 ring3、不触发后续阶段的 demand paging。

## 验证

新增默认关闭的 xmake 开关 `user_vmem_smoke`（define `BIGOS_USER_VMEM_SMOKE`）。启用后在
`kernel()` 中、IRQ 使能前的非中断上下文执行一次性验证 `bigos::mm::user_vmem_smoke()`：建立一个
`USER_DATA` 映射并读取 PTE 确认 user/writable/NX bit，确认 `USER_CODE` 清 NX，派生用户根确认
高/低半区条目布局，随后 unmap/释放并输出确定性 marker：

- 成功：`BIGOS_USER_VMEM_SMOKE_PASSED`
- 失败：`BIGOS_USER_VMEM_SMOKE_FAILED <stage>`

源码级测试 `tests/test_user_address_space_vmem_source.py` 固定 primitive 显式属性、内核默认属性
等价 supervisor `present+writable`、user/NX 策略、派生根高/低半区不变量，以及“派生不隐式写 CR3、
只有进程运行路径显式激活”的边界。

若需调整 `tools.bigosdev` 才能注入该开关并观测 marker，应作为独立横切工程化项处理，不混入
本 change。Bochs runtime smoke 依赖本机 ROM、image lock、serial oracle 与交互能力；不可用时按
既有惯例记录未运行原因与剩余 bootability 风险。

## 有界只读 file-backed 映射

除 anonymous、ELF-segment、guard backing 外，VMA 模型还携带一个只读 file-backed backing 类型。
file-backed VMA 记录支撑 `vfs::File` 引用与页对齐的起始文件偏移，落在专用的用户低半区
file-mapping 窗口（`USER_FILEMAP_BASE`，受 `USER_FILEMAP_MAX_PAGES` 约束），除非显式只读可执行
策略允许，否则始终只读且非可执行。它由有界 `SYS_MAP_FILE` 请求发布（只读、私有、offset/length
页对齐、不重叠、非 W+X），并按需惰性物化。

对该类 VMA 的 CPL3 not-present 读缺页，统一缺页入口计算 `文件偏移 = VMA 文件偏移 + (faulting page -
VMA 起始)`，经现有 page/buffer cache 读取覆盖该页的文件块（单页可能跨多个缓存块，任一块 IO 失败该
次物化确定性失败），建立只读非可执行用户 PTE，并推进 VMA 物化记账。对超出文件长度但仍在 VMA 范围
内的页尾部分按零填充处理，与 ELF 零填充约定一致。

这是对“非匿名 backing 且无恢复策略的 CPL3 缺页即 kill”规则的受控例外。对只读 file-backed 页的写
访问（present 或 not-present）、越界访问，以及需要在不可阻塞上下文发起阻塞缓存装入的物化，均保留既有
确定性 kill 语义；file-backed 页不进入 copy-on-write。`fork` 复制 file-backed VMA 元数据，自行 retain
其支撑文件引用，并以引用计数共享已物化的只读页而非深拷贝；未物化部分在各进程首次访问时独立重新缺页。
进程拆除与 exec 替换会释放每个 file-backed VMA 持有的文件引用，且不破坏仍被其他进程引用的共享只读缓存
状态。

默认关闭的 `file_backed_mapping_smoke` 开关（`xmake f --file_backed_mapping_smoke=y`）驱动映射创建、
首次访问物化命中正确文件内容、文件尾页零填充、对只读页写访问确定性 kill 以及越界访问确定性 kill，发射
`BIGOS_FILE_BACKED_MAPPING_PASSED` / `BIGOS_FILE_BACKED_MAPPING_FAILED`。它不是完整 POSIX `mmap`：不支持
可写/写回映射、`MAP_SHARED`、`MAP_FIXED` 或 swap。

## 有界 anonymous lifecycle

anonymous VMA 现在通过 `SYS_UNMAP_ANON` 与 `SYS_PROTECT_ANON` 支持有界主动 lifecycle 操作。两者都要求
页对齐、非空、位于用户低半区，并且目标范围必须由兼容 private anonymous VMA 完整覆盖。操作会先
staging 结果 VMA 集合，再发布 metadata，因此 prefix、suffix、middle split 与 capacity exhaustion
场景都有确定性结果。

`SYS_UNMAP_ANON` 会删除或拆分受影响 VMA，清除范围内 present user leaf PTE，释放 owned frame 或递减
COW/shared frame 引用，回收空的动态 owned 用户页表页，并 invalidation 受影响的当前 CPU translation。
lazy 且 not-present 的部分只删除 metadata，不会为了 unmap 而物化页面。

`SYS_PROTECT_ANON` 会更新 VMA 权限并 remap present PTE，使硬件访问权限不宽于 VMA policy。它拒绝
W+X 与 unsupported backing。当 VMA 仍允许写且页面仍为共享页时，COW marker 会被保留为 read-only
共享状态；权限收紧后，后续 demand-zero 与 COW fault 都以新的 VMA 权限为准。

freestanding userland 在 `user/libc/include/sys/mman.h` 暴露 BigOS-specific 的 `mmap_anon`、
`bigos_munmap_anon`、`bigos_mprotect_anon` wrapper 与 `PROT_*` 常量。这些 wrapper 文档化 bounded
BigOS 语义，不声明完整 POSIX 兼容。默认关闭的 `anonymous_lifecycle_smoke` 镜像覆盖 map、protect、
unmap、非法 W+X rollback、access-after-unmap 与 write-after-readonly 行为，marker 为
`BIGOS_ANON_LIFECYCLE_PASSED` / `BIGOS_ANON_LIFECYCLE_FAILED`。
