## Why

阶段 4 已经引入 cooperative 内核线程与单核 round-robin 调度，内核已经从“裸初始化”走向
“可运行多线程”。要进一步迈向用户态（阶段 6 加载第一个用户程序），第一步是把页表能力从
当前“仅服务内核 higher-half / KVMEM / direct map”的隐式约定，抽象成一个显式的、可复用的
map/unmap primitive，并明确 user / writable / NX 等页属性的 bit 策略。

当前 `kernel/mm/vmem.cc` 的页表代码只为内核范围服务：`map_kernel_range()` / `unmap_kernel_range()`
固定使用 `DEFAULT_ATTR_PTE = 0x3`（present + writable，supervisor），没有 user bit、没有 NX bit，
也没有“面向某个具体地址空间”的概念。在没有显式属性策略和地址空间抽象之前，无法安全地为用户态
建立独立映射。本 change 只做这一层准备工作，不实现 syscall 入口、不加载用户程序。

## What Changes

- 新增一个显式的内核虚拟内存映射 primitive 抽象（`bigos::mm` 命名空间下），用统一的页属性
  描述（present / writable / user / no-execute / global 等 bit）驱动 map / unmap，而不是在多处硬编码
  `0x3`。内核现有 `map_kernel_range()` 路径改为通过该 primitive 以 supervisor 默认属性表达，保持
  当前 PTE bit 行为不变。
- 定义 user / kernel 页表属性策略：明确 user 映射必须置 user bit、用户数据页置 NX、用户代码页清 NX，
  内核映射保持 supervisor 并默认 NX 数据 / 可执行代码的既有语义；属性与 `0x3` 旧常量的对应关系文档化。
- 引入“用户地址空间页表根”抽象的最小形态：能够基于内核当前 PML4 派生一个新的顶层页表，使内核
  higher-half / self-mapping / direct map 的高半区条目在用户地址空间中共享，而低半区（用户区）独立，
  为后续 ring3 切换预留。**本阶段不切换 CR3、不进入 ring3、不加载任何用户代码。**
- 增加默认关闭的构建开关与确定性 marker，用源码级 + 可选 emulator smoke 验证 map/unmap primitive 的
  属性 bit 正确性和高/低半区共享语义。

非目标（明确不在本 change 范围内）：

- 不定义 `syscall`/`sysret` 或 `int 0x80` 入口与 ABI（留给后续独立 change `define-syscall-entry`）。
- 不加载用户态 ELF、不实现 fork/exec/signal、不实现完整进程模型。
- 不实现 demand paging、copy-on-write，`#PF` handler 保持诊断-only。
- 不切换到 ring3、不在本阶段实际切换 CR3 运行用户代码。
- 不改动 boot 固定地址、higher-half/load base、BootInfo handoff ABI、`KVMEM_BASE`、direct map 区域或
  self-mapping 地址布局。

## Capabilities

### New Capabilities

- `user-address-space-vmem`: 显式页表 map/unmap primitive、user/kernel 页属性（writable/user/NX）策略、
  用户地址空间页表根的最小派生与高/低半区共享语义，以及对应的源码级与 emulator 验证要求。

### Modified Capabilities

（无。本 change 不改变已归档 capability 的既有 requirement；内核现有 `map_kernel_range()` 行为保持
等价，只是改为经由新 primitive 表达，不构成 spec-level 行为变化。）

## Impact

- 受影响子系统：内存管理（`kernel/mm/vmem.cc` 的页表 map/unmap 路径）与公共内存头
  `include/bigos/memory.h`（新增 primitive 声明），可能新增 `kernel/mm/` 下的页属性/地址空间辅助文件。
- 受影响假设：单核、早期关中断、无 SMP；保持 kernel-owned 静态 IDT 与中断契约不变；
  保持现有 `alloc_kernel_pages` / `free_pages` / direct map / self-mapping 地址语义不变。
- 构建：新增默认关闭的 xmake 验证开关与 marker；不改变默认 boot 行为。
- 工具链 / emulator：沿用 `x86_64-elf-gcc` cross-build 与 Bochs serial/VGA oracle；runtime smoke 在
  oracle 不稳定时记录剩余风险，与历史 change 一致。
