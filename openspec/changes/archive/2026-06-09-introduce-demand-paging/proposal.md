## Why

当前用户内存（`brk`、匿名映射、ELF 数据/BSS 段）在创建 VMA 时即逐页 eager 分配并映射物理页，唯一的惰性路径是 `try_handle_current_stack_fault` 专门处理向下增长的栈 VMA。`#PF` 处理器只对栈做恢复，其余用户态缺页一律 `fault_current_and_exit`，内核态缺页 panic。这一形态在进入 fork/COW/mmap 之前必须先一般化：路线图阶段 15 要求把现有栈缺页恢复泛化为统一缺页处理，作为第一块 POSIX 地基，且必须独立完成并独立验证。趁现在语义面小、消费者少时把缺页策略立稳，是后续阶段的安全前置。

## What Changes

- 把 `try_handle_current_stack_fault` 泛化为统一的用户态缺页处理入口，按 VMA 的 purpose / backing / growth / permissions 决定恢复策略，而不是仅识别栈 VMA。
- 为匿名 backing 的用户 VMA（`Anonymous` 匿名映射、`Heap`、向下增长 `Stack`、以及 ELF `.bss` 等零填充范围）引入惰性按需物化：缺页时分配并零填充单页、建立用户页表项，并推进对应 VMA 的 `materialized_start` / `materialized_end` 记账。
- 把 `brk` 扩展与 `map_anonymous_current` 的 eager 逐页映射改为登记惰性区间（仅更新 VMA / 边界元数据，不预先分配物理页），由统一缺页处理在首次访问时物化；保持失败回滚与边界可见性语义不变。
- 定义确定性失败语义：用户态合法缺页且物理页分配失败 -> 通过既有进程终止路径确定性 kill 当前进程（不 panic）；权限违例 / 非法地址 / 越界 / 保留位等不可恢复用户缺页 -> 确定性 kill；任何内核态（CPL0）缺页仍走统一 panic 路径。
- 新增默认关闭的验证开关 `demand_paging_smoke`（`BIGOS_DEMAND_PAGING_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_DEMAND_PAGING_PASSED` / `BIGOS_DEMAND_PAGING_FAILED`），覆盖惰性物化命中、分配失败 kill、权限违例 kill 等路径；保留现有 `page_fault_smoke` 与 `user_*_smoke` 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` ABI、IDT/向量布局、页表自映射地址、CR3 切换约定或用户低半区布局；`#PF` 仍是异常路径、不发 EOI。

## Capabilities

### New Capabilities
- `demand-paging`: 用户进程匿名 backing 内存的统一按需缺页物化与缺页策略——惰性零页物化、分配失败/权限违例的确定性进程 kill、内核态缺页 panic 边界，以及可复现的默认关闭验证开关。

### Modified Capabilities
- `vma-user-memory-api`: `brk` 扩展与受限匿名映射从「eager 逐页物化」改为「登记惰性区间、首次访问按需物化」的需求级行为变化，并统一栈/堆/匿名的物化记账语义。

## Impact

- 受影响子系统：`src/kernel/irq`（`page_fault_handler` 分发）、`src/kernel/proc`（缺页处理、`brk`、匿名映射、VMA 物化记账与进程 kill）。
- 受影响代码：[interrupt.cc](src/kernel/irq/interrupt.cc#L42-L61) 的 `page_fault_handler`、[proc.cc](src/kernel/proc/proc.cc#L1280-L1368) 的 `map_anonymous_current` / `brk_current` / `try_handle_current_stack_fault`、[proc.h](include/bigos/proc.h) 的相关声明。
- 构建/验证：`xmake.lua` 新增 `demand_paging_smoke` 开关；QEMU headless serial-marker smoke 与源码契约/行为断言测试。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 不变；CR2 取缺页地址、`error_code` 位含义（present/write/user/reserved/instruction-fetch）不变；用户低半区 VMA 布局与既有页属性常量不变；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：file-backed mmap、共享映射、swap、page cache、COW、fork、demand-zero 之外的填充策略、多页预取/超页、内核态惰性映射、用户态 libc。这些留给后续阶段（16/16.5/18/19）。
