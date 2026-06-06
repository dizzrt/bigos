## 1. 页属性与 map/unmap primitive

- [x] 1.1 在 `include/bigos/memory.h` 或等价 `bigos::mm` 头中声明显式页属性类型（present/writable/user/no-execute/global bit）与 map/unmap primitive 接口，标注 non-interrupt-context 使用约束。
- [x] 1.2 在 `src/mm/vmem.cc`（或新增 `src/mm/` 辅助文件）实现 primitive：复用现有 self-mapping 遍历、缺级页表分配与 `InterruptGuard` 写入边界，按属性入参设置 PTE bit。
- [x] 1.3 unmap primitive 清除 PTE 并对该虚拟地址执行 `invlpg` TLB 失效，保持与现有 `rollback_kernel_range()` 一致的失效语义。
- [x] 1.4 失败路径（缺级页表分配失败）保持现有 rollback 行为，不破坏 buddy/slab 阶段契约，不引入 IRQ handler 中的动态分配。

## 2. 内核映射改写与属性策略

- [x] 2.1 把 `VMem::map_kernel_range()` / `unmap_kernel_range()` 改为经由新 primitive，以等价 supervisor 默认属性（present+writable，user=0）表达，确认 PTE bit 行为与旧 `DEFAULT_ATTR_PTE=0x3` 一致。
- [x] 2.2 定义并文档化 user/kernel 属性策略：user 映射置 user bit、用户数据页置 NX、用户代码页清 NX、内核保持 supervisor；在 `docs/en/arch` 记录旧 `0x3` 与新属性的对应关系。
- [x] 2.3 确认 `EFER.NXE` 在 long-mode 进入路径的使能状态；若已使能则复用，否则记录 NXE 限制与 NX 验证降级策略，不在本 change 强行改 boot 路径。

## 3. 用户地址空间页表根派生

- [x] 3.1 实现基于内核当前 PML4 的用户地址空间根派生：分配一页新 PML4，复制高半区顶层条目（index 256..511），清零低半区（index 0..255）。
- [x] 3.2 用源码级检查固定本阶段不写 CR3、不进入 ring3、不加载用户代码、`#PF` 保持诊断-only；明确 self-mapping 在本阶段仍解析内核页表，per-space self-mapping 留待后续 change。

## 4. Smoke 与文档

- [x] 4.1 在 `xmake.lua` 新增默认关闭的 user vmem smoke 构建开关，在非中断上下文构造一次性验证：建立 user 属性映射并读取 PTE 确认 user/NX/writable bit、派生用户根确认高/低半区条目布局，随后 unmap/释放并输出确定性 `BIGOS_` marker（成功/失败）。
- [x] 4.2 更新 `docs/en/arch` 中内存/页表设计说明，记录显式属性 primitive、user/kernel 属性策略、用户根派生的高/低半区共享语义与本阶段非目标（不切 CR3、无 ring3、无 demand paging）。
- [x] 4.3 若需要调整 `tools/boot_debug.py` 才能注入 user vmem smoke 开关并观测 marker，单独记录为横切工程化项，不把 Python 修改混入本 change，除非明确扩展任务范围。

## 5. Validation

- [x] 5.1 新增或更新 `tests/test_user_address_space_vmem_source.py`，覆盖 primitive 接受显式属性、内核默认属性等价 supervisor present+writable、用户映射置 user bit、用户数据页 NX / 代码页非 NX、派生根复制高半区且清零低半区、本阶段不写 CR3 / 不进入 ring3、smoke marker wiring。
- [x] 5.2 运行 `uv run pytest tests/test_user_address_space_vmem_source.py tests/test_memory_interrupt_context_source.py` 及现有内存相关源码级测试，记录结果。
- [x] 5.3 运行默认 `xmake`，并在需要时运行 `user_vmem_smoke=y` 或等价构建配置，记录 cross-toolchain 构建结果；随后 `xmake f -c` 恢复默认配置。
- [x] 5.4 对新增/修改 C++ 源和头运行贴近 GCC cross-build 的 freestanding `x86_64-elf-g++ -fsyntax-only`，并按需用 clang/clangd 辅助诊断（freestanding C++17、x86_64 target、project include、无 hosted runtime、无异常、无 RTTI）；修复本 change 引入的诊断，区分历史诊断、本 change 诊断与工具链/freestanding 误报；若工具缺失记录 blocker 与剩余风险。
- [x] 5.5 运行已有 `mm_self_test` 构建/校验路径（在 oracle 可用时），确认内核映射改写未引入内存回归；若 Bochs/serial oracle 不可用，记录命令、失败点、历史 oracle 状态与剩余 bootability 风险。
- [x] 5.6 运行 `openspec validate prepare-user-address-space-vmem --strict`，并在 `validation.md` 中分开记录已通过检查、未运行或未通过检查、历史诊断、当前 change 影响和剩余风险。
