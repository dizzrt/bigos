## 1. 修复前确认

- [x] 1.1 复查 `proposal.md`、`design.md` 和 `specs/kernel-memory-correctness/spec.md`，确认本 change 只处理 VMem 布局语义、map/unmap 生命周期、失败回滚和 buddy split 元数据失败。
- [x] 1.2 复查 `kernel/mm/vmem.cc`、`kernel/mm/vmem.h`、`kernel/mm/buddy.cc`、`kernel/mm/buddy.h`、`kernel/mm/memdef.h` 和 `include/bigos/memory.h`，记录当前 `alloc_kernel_pages()`、`free_pages()`、`set_paging()`、`alloc_physical_order()` 的调用关系。
- [x] 1.3 确认本 change 不移动 `KVMEM_BASE`、`KVMEM_LEN`、`KERNEL_PML4_ADDR`、self-mapping 地址、低 2 MiB 保留区、kernel load base 或 higher-half base。
- [x] 1.4 确认本 change 不引入 scheduler、IRQ enable、SMP、direct map、用户态地址空间、页权限模型扩展、页表页回收框架或旧 allocator alias。
- [x] 1.5 确认本 change 保留 `free_pages()` 名称，不把 API polish rename 混入 VMem 生命周期修复。

## 2. VMem 布局语义

- [x] 2.1 在 `kernel/mm/vmem.cc` 或相邻头文件中补充简短注释，明确 `KVMEM_BASE` / `KVMEM_LEN` 管理 kernel heap/vmalloc-style virtual allocation 区，不是 direct map。
- [x] 2.2 保持 `alloc_kernel_pages(nr_pages, flags)` 的公开页数语义不变，确认普通调用方仍不需要直接理解 `_GFM_PRE_PAGING`。
- [x] 2.3 复查 `memdef.h` 中 `_GFM_PRE_PAGING` 注释，确保其语义仍限定为内部/低层预映射策略。
- [x] 2.4 复查 `include/bigos/memory.h` 中 `free_pages()` 注释，确认它仍明确配对 `alloc_kernel_pages()`，但不重命名为 `free_kernel_pages()`。
- [x] 2.5 更新或新增源码级测试，断言文档/源码中没有把 `KVMEM_BASE` 描述为 direct map 或 `virt = phys + offset` 区。

## 3. 页表 Map/Unmap 生命周期

- [x] 3.1 将 `VMem::set_paging()` 拆分或重命名为内部 map helper，保持只由 `alloc_kernel_pages()` 的映射路径调用。
- [x] 3.2 新增内部 unmap helper，按 `MemoryBlock::base`、`nr_pages` 和 `physical_area` 清除已映射 range 的 PTE。
- [x] 3.3 新增 x86_64 当前 CPU TLB invalidate helper，释放已映射页时对每个被清除的虚拟页执行 `invlpg` 或等价刷新。
- [x] 3.4 调整 `free_pages()` / `VMem::__free()`，确保对带 physical backing 的 range 先 unmap 和 flush，再调用 `free_physical_order()` 归还 backing。
- [x] 3.5 确保释放未映射的 reserved-only kernel virtual range 时 unmap helper 安全 no-op，且仍恢复 VMem free list 和 `nr_free_pages_`。

## 4. 映射失败回滚

- [x] 4.1 在 map helper 中记录本次映射已写入的 PTE 范围和本次新建的页表层级 descriptor。
- [x] 4.2 页表页分配失败时立即返回失败，不写入指向 0 或无效物理页的 present descriptor。
- [x] 4.3 如果 map helper 在部分 PTE 写入后失败，清除本次已写入 PTE，并对对应虚拟页执行 TLB invalidate。
- [x] 4.4 如果 `alloc_kernel_pages(..., _GFM_PRE_PAGING)` 在 backing 分配、backing list node 创建或 map helper 阶段失败，回收已分配 physical backing、清空 `physical_area` 节点，并把 VMem 区间恢复到 free list。
- [x] 4.5 明确页表页回收不是本 change 目标；若本次新分配页表页已经安全接入但后续失败，至少清除相关 descriptor 或 PTE，避免 dangling present entry。

## 5. Buddy Split 元数据失败

- [x] 5.1 调整 `Zone::alloc(order)` split 流程，记录原始 PageBlock 的 `base`、`len`、`order`、`flags` 和 `zone`。
- [x] 5.2 当剩余块的 `PageBlock` 或 intrusive list node 分配失败时，撤销本次已经创建并插入的剩余块节点。
- [x] 5.3 split 失败时恢复原始 PageBlock 并插回原 order free list，不扣减 zone/global free page 统计。
- [x] 5.4 split 失败时不把原始 node 插入 `gPageBlockList`，并向 `alloc_physical_order()` 返回 `nullptr`。
- [x] 5.5 复查 `__new_free()`、`free()` 和 `merge()`，确认新增失败恢复不会重新引入已删除 node 访问或统计漂移。

## 6. 测试与源码级检查

- [x] 6.1 更新 `tests/test_memory_correctness_source.py`，覆盖 map/unmap helper、PTE 清除、TLB invalidate、`KVMEM_BASE` 非 direct-map 语义和旧 API 禁用。
- [x] 6.2 为 buddy split 元数据失败恢复增加源码级断言，确认实现包含原始块恢复、失败返回和统计不扣减路径。
- [x] 6.3 为 `alloc_kernel_pages(..., _GFM_PRE_PAGING)` 失败回滚增加源码级断言，确认失败路径会释放 physical backing、清空 `physical_area` 并恢复 VMem 区间。
- [x] 6.4 运行 `uv run pytest tests/test_memory_correctness_source.py`，记录通过结果或失败原因。
- [x] 6.5 运行 `openspec validate --all`，确认 spec delta 可解析且不会破坏已归档 capability。

## 7. 构建与 C++ 辅助诊断

- [x] 7.1 运行 `xmake`，确认 x86_64-elf GCC cross build 通过；若工具链缺失，记录缺失命令和未验证风险。
- [x] 7.2 对修改过的 C++ 源文件和头文件运行尽量接近 freestanding x86_64 C++17 的 clang 辅助静态检查；若 clang 配置不可用，记录原因和剩余风险。
- [x] 7.3 使用 clangd 或 IDE diagnostics 检查修改过的文件，区分历史诊断、当前 change 新增诊断和 freestanding 误报。
- [x] 7.4 修复当前 change 引入的有效 C++ 编译错误、clang/clangd 诊断或源码级测试失败，再标记相关任务完成。

## 8. Boot 资产与运行时验证

- [x] 8.1 运行 `uv run python tools/boot_debug.py run --no-launch`，确认 kernel build、boot build、raw image 和 generated bochsrc 生成成功。
- [x] 8.2 若 Bochs、ROM 和本机运行条件可用，运行 bounded kernel boot smoke test，确认 `init_mem()` 后 kernel 能到达既有启动输出或等价可观察状态。
- [x] 8.3 若无法运行 Bochs runtime smoke，记录具体原因、已完成的替代验证和剩余 bootability 风险。

## 9. 收尾审查

- [x] 9.1 复查 `git diff`，确认本 change 未移动 boot/linker 地址、未引入 direct map、未恢复旧 allocator alias、未加入 scheduler/IRQ/SMP 假设。
- [x] 9.2 复查 `free_pages()` 路径，确认释放 mapped range 时不存在 PTE 残留后再释放 physical backing 的顺序错误。
- [x] 9.3 复查 map 失败路径，确认不写入无效 present descriptor，不保留半映射 range，不泄漏 backing list node。
- [x] 9.4 复查 buddy split 失败路径，确认 free list、allocated list 和 zone/global 统计在失败后保持一致。
- [x] 9.5 复查本 change 边界，确认没有引入 `free_kernel_pages()` rename、空页表页回收实现或 direct map 实现。
- [x] 9.6 汇总验证记录，明确列出已通过检查、未能运行检查、历史 warning、当前 change 新增或修复的诊断以及后续页表页回收/direct map 建议。


## 验证记录

- `uv run pytest tests/test_memory_correctness_source.py`：通过，12 passed。
- `openspec validate --all`：通过，10 passed, 0 failed。
- `xmake`：通过；保留既有 warning：命令行 macro whitespace、kernel LOAD segment RWX、`$(buildir)` deprecated。
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -Iinclude -Icpp/include -Icpp/libsupc++/include -Isrc -fsyntax-only kernel/mm/vmem.cc kernel/mm/buddy.cc`：通过；首次缺少 `cpp/libsupc++/include` 时 `<new>` 未找到，补充项目 freestanding C++ include 后通过。
- `GetDiagnostics`：`kernel/mm/vmem.cc`、`kernel/mm/vmem.h`、`kernel/mm/buddy.cc`、`tests/test_memory_correctness_source.py` 当前无诊断。
- `uv run python tools/boot_debug.py run --no-launch`：通过，kernel build、boot build、raw image 和 generated bochsrc 均生成成功；Bochs launch 按 `--no-launch` 跳过。
- Bochs runtime smoke：未运行。原因是当前工具链路径已验证到 boot asset 生成，但本次没有可自动判定 `init_mem()` 后启动输出的非交互 bounded smoke oracle；剩余风险是 emulator runtime bootability 未被本次会话直接观察。
- 边界复查：未移动 `KVMEM_BASE`、`KVMEM_LEN`、`KERNEL_PML4_ADDR`、self-mapping 地址、低 2 MiB 保留、kernel load base 或 higher-half base；未引入 direct map、scheduler、IRQ/SMP 假设、旧 allocator alias、`free_kernel_pages()` rename 或空页表页回收框架。
- 后续建议：页表页回收和 direct map 仍应作为独立 change 设计，不混入本次 VMem 生命周期修复。
