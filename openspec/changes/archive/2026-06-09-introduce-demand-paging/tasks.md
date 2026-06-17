## 1. 统一缺页处理入口

- [x] 1.1 在 `proc.h`/`proc.cc` 引入统一用户态缺页入口（如 `try_handle_user_page_fault(fault_addr, error_code)`），承接现有 `try_handle_current_stack_fault` 的职责，按覆盖该页的 VMA 的 purpose/backing/growth/permissions 决定恢复策略
- [x] 1.2 抽出共享的「匿名单页零物化」辅助（分配用户帧、`zero_frame`、`map_user_page_for_process`、推进 `materialized_*`），供栈/堆/匿名复用
- [x] 1.3 将向下增长栈的物化改写为统一入口下的一个分支，保留原有 `materialized_start` 边界与权限语义
- [x] 1.4 修改 `page_fault_handler`（[interrupt.cc](kernel/core/irq/interrupt.cc#L42-L61)）调用统一入口；用户态恢复失败 -> `fault_current_and_exit`，CPL0 缺页保持 `BIGOS_PAGE_FAULT` 诊断 + halt
- [x] 1.5 回归现有 `user_program_smoke` / `user_elf_smoke` / `fs_smoke`，确认仅切换入口不改变行为

## 2. 匿名 backing 惰性物化

- [x] 2.1 将 `map_anonymous_current`（[proc.cc](kernel/core/proc/proc.cc#L1280-L1341)）改为仅登记非重叠 VMA 与 `anon_next`，不再 eager 逐页分配映射；保持 unsupported 请求确定性拒绝与无部分发布
- [x] 2.2 将 `brk_current` 扩展（[proc.cc](kernel/core/proc/proc.cc#L1255-L1278)）改为仅更新 `heap_break` 与堆 VMA 边界元数据，不 eager 映射；扩展失败仅元数据回滚保持旧边界
- [x] 2.3 修正 `brk` 收缩与进程 teardown/reclaim，仅 unmap 真正已物化的页（依据 `materialized_*`），避免对未物化页执行 unmap/free
- [x] 2.4 确认 ELF 数据/BSS 等零填充范围在统一入口下按匿名惰性物化（如适用），不破坏既有 ELF 加载与权限

## 3. 确定性失败语义

- [x] 3.1 在统一入口内区分并处理：分配失败、权限违例（present 位）、越界、非匿名 backing、不可阻塞上下文，全部对 CPL3 走确定性进程 kill
- [x] 3.2 确认对仅 Read/Write 页保持 NO_EXECUTE，取指缺页在这些页上视为非法 kill，不物化为可执行
- [x] 3.3 确认 kill 路径不留部分页表映射、不 panic 内核

## 4. 验证开关与测试

- [x] 4.1 在 `xmake.lua` 新增默认关闭的 `demand_paging_smoke` -> `BIGOS_DEMAND_PAGING_SMOKE`，发射 `BIGOS_DEMAND_PAGING_PASSED/FAILED`，覆盖惰性物化命中、分配失败 kill、权限违例 kill
- [x] 4.2 添加/更新源码契约或行为断言测试（`tests/`，`uv run pytest`），覆盖统一入口、惰性 brk/匿名、收缩按物化 unmap、失败语义
- [x] 4.3 跑 QEMU headless serial-marker smoke（`tools/boot_debug.py`）验证 `BIGOS_DEMAND_PAGING_PASSED`；记录 QEMU/Bochs/工具链不可用时的跳过原因与残余风险
- [x] 4.4 `openspec validate introduce-demand-paging --strict` 通过；确认未移动 boot/higher-half/direct-map/KVMEM/自映射/syscall 向量/EOI 语义

## 5. 文档与归档准备

- [x] 5.1 同步 `roadmap.md` demand paging capability 状态（proposed -> 实现/验证记录），保持 `docs/en` canonical 与 `docs/zh` 镜像（如涉及）
- [x] 5.2 记录架构/内存布局/emulator/toolchain 假设与非目标到 change 的验证笔记，准备 `openspec archive`

## 验证笔记 / Validation Notes

- 构建：`xmake f --demand_paging_smoke=y && xmake` 通过（`x86_64-elf-*` 交叉工具链，release）。预存的 `LOAD segment with RWX permissions` 链接告警与本次改动无关。
- 源码契约：`uv run pytest tests/ -q` 全绿（174 passed），新增 `test_unified_page_fault_entry_decides_by_vma_metadata`、`test_demand_paging_smoke_is_default_off_and_marker_emitting`，并将 brk/匿名用例改为惰性语义断言。
- 运行时 smoke：`uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_DEMAND_PAGING_PASSED` 观测到 `BIGOS_DEMAND_PAGING_PASSED`（覆盖惰性物化命中、注入式分配失败 kill、present-bit 权限违例 kill、越界 reject）。
- 回归：`user_program_smoke` 与 `user_elf_smoke` 均仍观测到 `BIGOS_USER_EXIT`，确认仅切换缺页入口未改变既有 ring3 行为。
- 严格校验：`openspec validate introduce-demand-paging --strict` 通过。
- 边界未移动：未改动 boot 固定地址、higher-half、direct-map、`KVMEM_BASE`、递归自映射、`int 0x80` 向量或异常/IRQ EOI 语义（`test_low_level_boundaries_are_not_moved_or_widened` 守护）。
- 假设：x86_64 单核、同步；CR2 取缺页地址、`error_code` 位（present/write/user/instruction-fetch）语义不变；用户低半区 VMA 布局与页属性常量不变。
- 非目标（留待后续阶段）：file-backed mmap、共享映射、swap、page cache、COW、fork、demand-zero 之外填充、多页预取/超页、内核态惰性映射、用户态 libc。
- 残余风险：未在 Bochs 上交叉验证（本次改动不涉及早期 boot/实模式/长模式/ATA PIO/端口 IO，QEMU headless 已足够）；smoke 在内核线程上下文驱动统一入口而非真实 ring3 取指，取指缺页与 NX 交互依赖统一入口内的显式断言与源码契约测试覆盖。
