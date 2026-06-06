# 用户地址空间页表准备

本阶段把 `src/mm/vmem.cc` 中“仅服务内核范围”的页表代码抽象成显式的 map/unmap
primitive，并定义 user/kernel 页属性策略与用户地址空间页表根的最小派生。**本阶段不切换
CR3、不进入 ring3、不加载用户代码、不实现 demand paging / COW。**

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

一对 primitive：

- `bool map_page(uint64_t vaddr, uint64_t phys, PageAttr attr)`：建立单个 4 KiB 映射，
  复用现有递归 self-mapping 遍历与缺级页表分配，缺级失败时回滚已创建的中间级并返回
  `false`。当 `attr` 是 user 映射时，中间级条目继承 user bit 以保证叶子页可达；NX 只在叶子
  PTE 上编码。
- `void unmap_page(uint64_t vaddr)`：清除 PTE 并对该地址执行 `invlpg`，与现有
  `rollback_kernel_range()` 的失效语义一致。

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

当前 long-mode 进入路径 `src/arch/x86/boot/boot.s` 在 `IA32_EFER`（MSR `0xc0000080`）只置
`LME`（bit 8），**未使能 NXE（bit 11）**。因此本阶段 NX bit 仅作为“属性编码正确”验证，不能
依赖运行时强制不可执行。本 change 不修改 boot 路径来使能 NXE；真正强制 NX 留待后续切换 CR3 /
进入 ring3 的 change，届时需在 boot 或内核早期使能 EFER.NXE 并重做 runtime NX 验证。

**剩余风险**：在 NXE 未使能时，若未来代码错误地依赖 NX 运行时保护，将得不到硬件强制；本阶段以
源码级编码检查覆盖该差距并显式记录。

## 用户地址空间页表根派生

`uint64_t derive_user_address_space_root()`：

- 分配一页新 PML4，通过 direct map 访问它。
- **复制内核 PML4 高半区顶层条目**（index 256..511，覆盖 kernel higher-half、self-mapping、
  direct map、KVMEM），使内核地址在派生根中共享。
- **清零低半区条目**（index 0..255），保证用户区相互独立。
- 返回新根物理地址，失败返回 `INVALID_PHYS_ADDR`。

**self-mapping 语义**：本阶段共享内核高半区，派生根中的 self-mapping 槽位仍解析内核页表；
“每地址空间独立 self-mapping”留待真正切换 CR3 的后续 change 设计。**本阶段不写 CR3、不切换
地址空间。**

## 验证

新增默认关闭的 xmake 开关 `user_vmem_smoke`（define `BIGOS_USER_VMEM_SMOKE`）。启用后在
`kernel()` 中、IRQ 使能前的非中断上下文执行一次性验证 `bigos::mm::user_vmem_smoke()`：建立一个
`USER_DATA` 映射并读取 PTE 确认 user/writable/NX bit，确认 `USER_CODE` 清 NX，派生用户根确认
高/低半区条目布局，随后 unmap/释放并输出确定性 marker：

- 成功：`BIGOS_USER_VMEM_SMOKE_PASSED`
- 失败：`BIGOS_USER_VMEM_SMOKE_FAILED <stage>`

源码级测试 `tests/test_user_address_space_vmem_source.py` 固定 primitive 显式属性、内核默认属性
等价 supervisor `present+writable`、user/NX 策略、派生根高/低半区不变量，以及“本阶段不写 CR3 /
不进入 ring3、`#PF` 保持诊断-only”。

若需调整 `tools/boot_debug.py` 才能注入该开关并观测 marker，应作为独立横切工程化项处理，不混入
本 change。Bochs runtime smoke 依赖本机 ROM、image lock、serial oracle 与交互能力；不可用时按
既有惯例记录未运行原因与剩余 bootability 风险。
