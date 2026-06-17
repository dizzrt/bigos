## Context

kernel thread scheduler capability 完成后，内核已具备 cooperative 内核线程与单核 round-robin 调度。下一步迈向用户态前，
需要把当前“仅服务内核”的页表代码抽象成显式的 map/unmap primitive，并定义 user/kernel 页属性策略。

当前页表实现位于 `kernel/mm/vmem.cc`：

- 内核拥有静态 PML4，物理地址 `KERNEL_PML4_ADDR = 0x2000`，并通过递归 self-mapping
  （`SELF_MAPPING_BASE = 0xffff800000000000`）访问各级页表。
- 地址布局已分区：higher-half 内核 `0xffffffff80000000`、`KVMEM_BASE = 0xffff880000000000`
  （heap/vmalloc）、self-mapping、`KDIRECT_BASE = 0xffff900000000000`（direct map）。这些都在
  **高半区**（PML4 index >= 256）。
- `VMem::map_kernel_range()` / `unmap_kernel_range()` 逐页建立映射，固定使用
  `DEFAULT_ATTR_PTE = 0x3`（present + writable，**supervisor，无 NX，无 user bit**），缺页表级时用
  `alloc_physical_order(0, 0)` 分配并在 `InterruptGuard` 下写入条目。
- 所有 map/unmap 都隐式作用于内核当前 PML4，没有“目标地址空间”参数，也没有显式属性入参。

约束：单核、早期关中断、无 SMP；freestanding C++17、无异常 / 无 RTTI；保持 kernel-owned 静态 IDT
与 `InterruptFrame` ABI；不得改动 boot 固定地址、higher-half/load base、`KVMEM_BASE`、direct map 区域、
self-mapping 地址与 BootInfo handoff ABI。

## Goals / Non-Goals

**Goals:**

- 提供显式页属性描述（present / writable / user / NX / global 等 bit）与 map/unmap primitive，取代
  分散硬编码的 `0x3`，并让内核现有映射路径以等价 supervisor 默认属性表达，PTE bit 行为保持不变。
- 文档化并以源码级检查固定 user/kernel 属性策略：user 映射必置 user bit、用户数据页置 NX、用户代码页
  清 NX；内核映射保持 supervisor。
- 引入用户地址空间页表根的最小派生：基于内核 PML4 复制高半区条目（共享 kernel/self-mapping/direct map），
  低半区独立清零，供后续 ring3 阶段使用。
- 用默认关闭的构建开关 + 确定性 marker（源码级必测、emulator 可选）验证属性 bit 与高/低半区共享语义。

**Non-Goals:**

- 不定义 syscall/sysret 或 int 0x80 入口与 ABI（后续 `define-syscall-entry`）。
- 不加载用户 ELF、不实现进程模型、fork/exec/signal。
- 不实现 demand paging / COW；`#PF` 保持诊断-only。
- 不切换 CR3 运行用户代码、不进入 ring3。
- 不改动既有地址布局常量、boot/handoff ABI 或 self-mapping 语义。

## Decisions

### 决策 1：显式页属性类型 + primitive，而非继续硬编码 bit

引入一个 `bigos::mm` 下的页属性抽象（例如 `PageAttr` / `PageFlags`，封装 present/writable/user/NX/global），
和一对显式 primitive：`map_page(root, vaddr, phys, attr)` / `unmap_page(root, vaddr)`（命名以实现为准）。
内核现有 `map_kernel_range()` 改为以 supervisor 默认属性（等价于旧 `0x3`，即 present+writable，user=0，NX=0）
调用 primitive。

- 备选：保留 `0x3` 常量、仅新增并行的 user 版本。否决：会出现两套页表写入逻辑，self-mapping 遍历、
  缺页表级分配、`InterruptGuard` 边界与 rollback 都要重复，易产生漂移。复用单一 primitive 更安全。
- NX bit 需要 PTE bit 63，要求 EFER.NXE 已使能；本 change 先用源码级方式确认/记录该前提，运行时使能
  策略若已存在则复用，否则记录为风险，不在本阶段强行改 boot 路径。

### 决策 2：属性策略以默认 supervisor、显式 user 表达

- 内核映射：user=0，writable 按用途，NX 按用途（保持现状不回归）。
- 用户映射：user=1 必置；用户数据页 NX=1、用户代码页 NX=0、按需 writable。
- 旧 `0x3` 与新属性的对应关系在 `docs/en/arch` 文档化，并由源码级测试断言内核默认属性等价 `0x3` 行为，
  防止把 user/NX 语义误加到内核范围。

### 决策 3：用户地址空间根 = 复制高半区、低半区独立

- 新派生根：分配一页作为新 PML4，**复制内核 PML4 的高半区条目**（index 256..511，覆盖 kernel
  higher-half、self-mapping、direct map、KVMEM），**低半区条目清零**（index 0..255，用户区独立）。
- 这样内核地址在任何地址空间都一致可见，符合标准 higher-half 设计；用户低半区相互隔离。
- 本阶段只构造与验证该根的条目布局，**不写 CR3、不切换**。self-mapping 在新根中的正确性（self-mapping
  条目指向新根自身还是共享内核根）作为关键设计点，在 design 中固定为：本阶段共享内核高半区，self-mapping
  仍解析内核页表；对“每地址空间独立 self-mapping”的需求留待真正切换 CR3 的后续 change。
- 备选：lazy 同步内核高半区。否决：当前内核高半区在 boot 后基本静态，直接复制顶层条目即可共享，无需
  同步机制；复杂同步留待后续。

### 决策 4：验证用默认关闭构建开关 + 确定性 marker

新增默认关闭的 xmake 开关（例如 `user_vmem_smoke`），在非中断上下文构造一次性验证：
建立一个 user 属性映射、读取其 PTE 确认 user/NX/writable bit，派生一个用户根确认高/低半区条目布局，
然后 unmap/释放，输出确定性 `BIGOS_` marker（成功 / 失败）。默认 boot 行为不变。

## Risks / Trade-offs

- [NX bit 依赖 EFER.NXE] → 若当前 boot/long-mode 路径未使能 NXE，置 NX bit 可能 #GP 或被忽略。
  缓解：源码级确认 NXE 状态，未使能则文档化并将 NX 验证降级为“属性编码正确”而非运行时强制，记录剩余风险。
- [self-mapping 与多地址空间] → 复制高半区共享内核 self-mapping，未来真正切换 CR3 时需要重新设计
  per-space self-mapping。缓解：本阶段不切换 CR3，明确把该问题列为后续 change 的前置设计。
- [改写内核 map 路径引入回归] → 把 `map_kernel_range()` 改为经由 primitive 可能改变 PTE bit。
  缓解：源码级测试断言内核默认属性等价旧 `0x3`，并跑现有 `mm_self_test` 与内存源码级测试确认无回归。
- [emulator oracle 不稳定] → 历史 change 多次出现 Bochs serial 30~40s 未观测 marker。缓解：以源码级检查
  为必测，runtime smoke 为可选，oracle 不可用时按既有惯例记录命令、失败点与剩余 bootability 风险。
- [中断安全] → primitive 仍在非中断上下文调用，页表写入沿用 `InterruptGuard` 屏蔽 same-CPU IRQ 交织，
  与kernel memory API capability 契约一致；不在 IRQ handler 中调用。

## Migration Plan

1. 先加属性类型与 primitive，让 `map_kernel_range()` 改用 primitive 并验证等价（无行为变化）。
2. 再加 user 属性策略与用户根派生（纯构造 + 读取验证，不切 CR3）。
3. 加默认关闭 smoke 开关与 marker，补源码级测试与文档。
回滚策略：primitive 与 user 根派生是新增，smoke 默认关闭；如需回滚，恢复 `map_kernel_range()` 直接写
`0x3` 即可，地址布局与 boot 路径未改，风险可控。

## Open Questions

- EFER.NXE 在当前 long-mode 进入路径是否已使能？（实现阶段需在 boot/CR 寄存器代码中确认并记录。）
- 用户根派生后续真正切换 CR3 时，self-mapping 是否需要 per-space 独立 recursive 槽位？（留待
  `define-syscall-entry` / ring3 切换 change 设计，本阶段不决策。）
