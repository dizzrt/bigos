## Context

BigOS 已有进程级 VMA 集合与一条专用的栈缺页恢复路径 `try_handle_current_stack_fault`：它仅处理向下增长的 `Stack` VMA，在 `materialized_start` 之上、`start` 之下的页上分配零页并映射。其余用户内存（匿名映射、`brk` 堆、ELF 数据/BSS）在创建 VMA 时即 eager 逐页物化。`page_fault_handler`（[interrupt.cc](file:///Users/bytedance/Desktop/workspace/kernel/bigos/src/kernel/irq/interrupt.cc#L42-L61)）在 `BIGOS_USER_PROCESS` 下，对用户态缺页先尝试栈恢复，失败即 `fault_current_and_exit(-14)`；其余路径打印 `BIGOS_PAGE_FAULT` 诊断并 `halt_cpu()`。

约束：freestanding、单核、同步；`#PF` 异常路径不发 EOI、不可重入阻塞（`NonblockingContextGuard`）；CR2/error_code 位语义固定；用户低半区布局、页属性常量、CR3 切换不变。本设计是路线图阶段 15，必须独立完成并独立验证，作为 fork/COW/mmap 的前置。

VMA 结构已具备记账字段：`materialized_start`（向下增长用）与 `materialized_end`（向上增长 / 区间用），可直接承载惰性物化边界，无需新增结构。

## Goals / Non-Goals

**Goals:**
- 把栈专用恢复泛化为统一用户态缺页处理：按 VMA 的 purpose/backing/growth/permissions 决定是否物化。
- 匿名 backing VMA（匿名映射、堆、向下栈、ELF BSS 零填充范围）支持惰性单页零物化。
- `brk` 扩展与匿名映射改为登记惰性区间而非 eager 映射，保留失败回滚与边界可见性语义。
- 确定性失败：合法缺页分配失败 -> 进程 kill；不可恢复用户缺页 -> 进程 kill；内核态缺页 -> panic。
- 默认关闭、可复现的 marker 验证（`demand_paging_smoke`）。

**Non-Goals:**
- file-backed mmap、共享映射、swap、page cache、COW、fork。
- demand-zero 以外的填充、多页预取、超页、内核态惰性映射、用户态 libc。

## Decisions

- **统一入口而非分散判断**：用 `try_handle_user_page_fault(fault_addr, error_code)` 取代 `try_handle_current_stack_fault` 作为 `page_fault_handler` 的恢复入口。该函数先做通用前置校验（有进程、Running、可阻塞、present 位为 0 表示非保护违例、instruction-fetch 位不强制拒绝执行段），再定位覆盖该页的 VMA，按 VMA 元数据决定物化策略。栈的特殊「在 materialized_start 之下扩展」逻辑作为该统一入口内的一个分支保留。
  - 备选：保留多个独立 `try_handle_*` 函数并在 handler 里依次尝试。否决：随 VMA purpose 增多会变成脆弱的 if 链，与「泛化」目标相悖。
- **物化记账复用现有字段**：向下增长用 `materialized_start`，向上 / 固定区间用 `materialized_end`；惰性区间登记后令未物化区域满足 `page >= materialized_start`（向下）或 `page >= materialized_end`（向上/固定）即视为「已登记未物化」，缺页时单页物化并推进对应边界。
  - 备选：新增 per-page bitmap。否决：MAX_VMAS 有界、单页推进足够，避免引入新结构与初始化路径。
- **brk / 匿名映射改惰性**：`brk_current` 扩展时仅更新 `heap_break` 与 VMA `end`，不预先 `alloc_user_frame`/`map`；收缩时仍即时 unmap 已物化页并回退 `materialized_end`。`map_anonymous_current` 仅登记 VMA 与 `anon_next`，物化交给缺页。回滚因此简化为「仅元数据回滚」，但需保证收缩 / teardown 只 unmap 真正已物化的页（依据 `materialized_*`）。
  - 备选：保持 eager。否决：与阶段目标冲突，且 eager 堆/匿名在大区间上浪费物理页。
- **失败语义集中在 handler**：物化成功返回 true；分配失败 / 权限不符 / 越界 / 非匿名 backing -> 返回 false。`page_fault_handler` 对用户态 false 一律 `fault_current_and_exit`（确定性 kill），内核态缺页保持现有 panic/halt。区分「分配失败」与「非法访问」仅体现在 kill 原因码，不改变是否 kill。
- **验证开关**：新增 `demand_paging_smoke` -> `BIGOS_DEMAND_PAGING_SMOKE`，发射 `BIGOS_DEMAND_PAGING_PASSED/FAILED`，默认关闭，纳入 stage 9 QEMU headless 矩阵；不删除 `page_fault_smoke` 与 `user_*_smoke`。
- **kill 原因码复用 fault_reason**：「分配失败 kill」与「非法访问 kill」不分配新的 `BIGOS_*` 诊断码，统一复用既有 `fault_reason`（沿用 `-14` 缺页约定），由 marker 附带 `error_code` 位（present/write/user/instruction-fetch）供行为断言区分两类原因。
  - 备选：为两类失败各引入独立诊断码/原因码。否决：增加 ABI 面与维护成本，error 位已足够区分，且与现有统一 fault 路径一致。
- **失败注入走 smoke 专用钩子**：`demand_paging_smoke` 的分配失败路径用 smoke 专用、默认关闭的注入钩子（在该 smoke 上下文内强制 `alloc_user_frame` 返回 0），而非全局测试构建宏。
  - 备选：用全局测试构建宏注入。否决：会让失败注入泄漏到非 smoke 构建路径，违背默认关闭与最小耦合原则。

## Risks / Trade-offs

- [惰性化改变 teardown / 收缩的页计数] → 收缩与 reclaim 必须严格按 `materialized_*` 边界 unmap，避免对未物化页执行 unmap/free；新增针对部分物化区间的回收测试。
- [统一入口误把内核态或保护违例缺页当可恢复] → 前置校验显式要求 present 位为 0 且为用户态上下文，权限违例（present=1）直接走 kill；保留内核态 panic 不变。
- [惰性堆/匿名首次访问引入新缺页路径，可能在不可阻塞上下文触发] → 沿用 `can_block()` 前置校验，不可阻塞时不物化、按确定性 kill 处理（与现状一致）。
- [行为变化可能破坏依赖 eager 物化的现有 smoke] → 先跑现有 `user_program_smoke` / `user_elf_smoke` / `fs_smoke` 回归，再启用新 smoke；记录 QEMU/Bochs/工具链不可用时的跳过原因。
- [error_code instruction-fetch 与 NX 段交互] → 对仅 Read/Write 权限页保持 NO_EXECUTE，取指缺页在这些页上视为非法 -> kill，不物化为可执行。

## Migration Plan

1. 引入统一缺页入口与惰性物化辅助，保持 `brk`/匿名映射 eager 行为暂不变，仅切换 handler 调用，回归现有 smoke。
2. 将 `brk` 扩展与匿名映射切换为登记惰性区间；同步修正收缩 / teardown 的按 `materialized_*` unmap。
3. 新增 `demand_paging_smoke` 与 marker，纳入 stage 9 矩阵与源码契约 / 行为断言测试。
4. 回滚策略：保留旧 eager 路径为编译期可还原的最小补丁边界；若惰性化暴露回收缺陷，可先回退第 2 步、保留第 1 步统一入口。
