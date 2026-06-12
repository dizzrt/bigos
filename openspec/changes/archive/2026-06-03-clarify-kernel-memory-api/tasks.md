## 1. 修复前确认

- [x] 1.1 复查 `proposal.md`、`design.md` 和 `specs/kernel-memory-correctness/spec.md`，确认本 change 只处理内存 API 语义澄清与旧 alias/遗留声明清理。
- [x] 1.2 搜索当前仓库中 `alloc_pages(`、`alloc_kernel_pages(`、`alloc_physical_pages(`、`alloc_physical_order(`、`free_physical_pages(`、`_GFM_PRE_PAGING` 的声明和调用点，记录需要迁移的文件。
- [x] 1.3 确认本 change 不移动 `KVMEM_BASE`、`KERNEL_PML4_ADDR`、self-mapping 地址、低 2 MiB 保留区、kernel load base 或 higher-half base。

## 2. 公开 Kernel Virtual Pages API

- [x] 2.1 从 `include/bigos/memory.h` 移除 `alloc_pages(uint32_t, gfm_t)` 声明，保留 `alloc_kernel_pages(uint32_t, gfm_t)`。
- [x] 2.2 为 `alloc_kernel_pages()` 和 `free_pages()` 添加简短注释，说明二者按 kernel virtual pages 语义配对，参数是页数而不是 buddy order。
- [x] 2.3 从 `kernel/mm/vmem.cc` 移除 `alloc_pages()` wrapper 定义，确保新代码不能继续使用旧公开别名。
- [x] 2.4 搜索并迁移仓库内所有 `alloc_pages(` 调用点；如果没有外部调用点，记录源码扫描结果。

## 3. 内部 Physical Order API

- [x] 3.1 从 `kernel/mm/buddy.h` 移除 `alloc_physical_pages()` 和 `free_physical_pages()` 声明，只保留 `alloc_physical_order()` 和 `free_physical_order()`。
- [x] 3.2 从 `kernel/mm/buddy.cc` 移除 `alloc_physical_pages()` 和 `free_physical_pages()` wrapper 定义。
- [x] 3.3 搜索并迁移仓库内所有 `alloc_physical_pages(` 与 `free_physical_pages(` 调用点到 order 语义明确的接口。
- [x] 3.4 复查 `kernel/mm/vmem.cc` 的页表页分配和 backing 分配路径，确认它们继续使用 `alloc_physical_order(order, flags)`。

## 4. 遗留声明与 Flag 边界

- [x] 4.1 清理 `kernel/mm/kmem.h` 中未实现或未使用的 `kmem_cache_alloc()`、`kmem_memory_alloc_pages()` 声明。
- [x] 4.2 复查 `kernel/mm/kmem.cc`、`kernel/mm/slab.cc`、`cpp/libsupc++/new.cc` 和 KTL allocator 调用路径，确认普通 `kmalloc/new` 调用方不需要传 `_GFM_PRE_PAGING`。
- [x] 4.3 为 `_GFM_PRE_PAGING` 添加或调整简短注释，说明它是内存管理内部/低层预映射策略，不是普通 `kmalloc()` 调用方需要使用的 flag。
- [x] 4.4 按 `design.md` 决策记录 `free_pages()` 不因名称对称单独重命名为 `free_kernel_pages()` 的原因，并确保注释足以避免和 physical page free 混淆。

## 5. 测试与静态检查

- [x] 5.1 更新或新增源码级测试，断言 `alloc_pages(`、`alloc_physical_pages(`、`free_physical_pages(`、`kmem_memory_alloc_pages` 不再出现在公开头文件和 `kernel/mm` 实现中。
- [x] 5.2 更新或新增源码级测试，断言 slab grow 使用 `alloc_kernel_pages(1u << buddy_order_, ...)`，VMem backing 使用 `alloc_physical_order(...)`。
- [x] 5.3 运行 `python3 -m pytest tests/test_memory_correctness_source.py`；若缺少 `pytest`，使用可记录的替代方式执行测试函数，并说明验证缺口。
- [x] 5.4 运行 `openspec validate --all`，确认 delta spec 可解析且不破坏已归档 capability。
- [x] 5.5 运行 `xmake`，确认 `x86_64-elf-*` 交叉工具链构建通过；若工具链缺失，记录缺失命令和无法验证的风险。

## 6. C++ 辅助诊断与 Boot 验证

- [x] 6.1 对修改过的 C++ 源文件和头文件运行尽量接近 freestanding x86_64 C++17 的 clang 辅助静态检查；若 clang 配置不可用，记录原因和剩余风险。
- [x] 6.2 使用 clangd 或 IDE diagnostics 检查修改过的文件，区分历史诊断、当前 change 新增诊断和 freestanding 误报。
- [x] 6.3 若 Bochs 和 disk image 配置可用，运行 kernel boot smoke test，确认 `init_mem()` 后 kernel 仍能到达既有启动输出。
- [x] 6.4 若 Bochs 或镜像路径不可用，记录无法运行 boot smoke 的原因，并说明本 change 主要通过构建和源码级 API 检查覆盖。

## 7. 收尾审查

- [x] 7.1 复查 `git diff`，确认本 change 未包含 `set_paging()` 失败回滚、buddy split 元数据失败处理、direct map、early allocator 或地址布局变更。
- [x] 7.2 汇总验证记录，列出通过检查、未能运行的检查、历史 warning、当前 change 新增或修复的诊断。
- [x] 7.3 确认 tasks 中每项完成状态与实际验证一致，再准备归档；若进入后续 `stabilize-kernel-vmem-layout` change，以 `reserve_kernel_pages()` / `alloc_mapped_kernel_pages()` 替代普通调用方可见 `_GFM_PRE_PAGING` 语义作为设计方向。

## 验证记录

- `python3 -m pytest tests/test_memory_correctness_source.py` 未运行：当前 Python 环境缺少 `pytest`。
- 已用替代方式直接导入并执行 `tests/test_memory_correctness_source.py` 内全部 `test_*` 函数，7/7 通过。
- `openspec validate --all` 通过，10/10 items passed。
- `xmake` 通过；保留历史/构建配置 warning：命令行宏缺少空格、`build/kernel` 存在 RWX LOAD segment、`$(buildir)` deprecated。
- `clang++ --target=x86_64-elf ... -fsyntax-only` 对 `kernel/mm/vmem.cc`、`kernel/mm/buddy.cc`、`kernel/mm/kmem.cc`、`kernel/mm/slab.cc` 通过。
- IDE diagnostics 未报告新增诊断。
- Bochs 位于 `/opt/homebrew/bin/bochs`，但仓库缺少 `test/bochsrc.bxrc` 或其他 bochs 配置文件，未运行 boot smoke；本 change 由构建、clang 辅助检查和源码级 API 扫描覆盖。
