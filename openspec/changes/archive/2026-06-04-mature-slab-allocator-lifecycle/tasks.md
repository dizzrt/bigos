## 1. 设计与边界确认

- [x] 1.1 复查 `proposal.md`、`design.md` 和 spec delta，确认本 change 只成熟化 slab 生命周期，不引入并发 allocator。
- [x] 1.2 复查 `src/mm/slab.*`、`src/mm/kmem.*`、`src/mm/vmem.*` 和 `include/bigos/memory.h`，记录当前 small-object、dynamic slab 和 `free()` 分发路径。
- [x] 1.3 复查 `src/mm/buddy.cc` early metadata arena baseline，确认初始化期 `PageBlock`/list node 仍由 arena 提供，当前 change 不改变 arena 来源、容量、生命周期或 BootInfo memory map ABI。
- [x] 1.4 确认本 change 不移动 `KVMEM_BASE`、self-mapping 地址、boot/linker 地址或公开页分配 API。

## 2. 统计与 Debug 基础

- [x] 2.1 增加 slab/cache 统计结构或打印路径，覆盖 size class、slab 数、对象数、free/full 状态。
- [x] 2.2 增加 large allocation 统计字段或验证接口，覆盖 outstanding count 和 held pages。
- [x] 2.3 增加可开关的 slab debug guard 配置，默认不改变普通 boot 行为。
- [x] 2.4 在 debug build 中实现 double-free、非法边界和 poison pattern 的最小检测。

## 3. Large Allocation 路径

- [x] 3.1 设计并实现 large allocation header，使 `free()` 能区分 slab object 与 page-backed large allocation。
- [x] 3.2 修改 `kmalloc()` / CacheChain fallback，为超过 `CACHE_MAX_OBJ_SIZE` 的请求分配足够 kernel virtual pages，并保持当前 small-object 上限为 2048B。
- [x] 3.3 实现 large allocation 失败回滚，释放已获取的 pages 和 metadata。
- [x] 3.4 修改 `free()`，确保 large allocation 正确调用 `free_pages()` 且 double free 在 debug build 中可诊断。

## 4. 空 Slab 回收

- [x] 4.1 为 `Cache::free()` 增加动态 slab 完全空闲判断，区分 `SLAB_PERMANENT` 和动态 slab。
- [x] 4.2 实现 cache reclaim policy：每个 cache 至少保留一个可用 slab，避免释放后立即重新扩容。
- [x] 4.3 实现动态空 slab 从 avl/full list 摘除、释放 bitmap、Slab 元数据、list node 和 backing pages 的顺序。
- [x] 4.4 增加失败路径复查，确认回收不会访问已释放 list node 或 slab metadata。

## 5. Perfect-Fit 语义收敛

- [x] 5.1 明确禁用 `_GFM_NEW_CACHE_TO_PFIT` 自动动态 cache 创建语义，保留 `GFM_PERFECT_FIT` 作为已有 cache 的精确匹配约束。
- [x] 5.2 更新 flag 注释、组合宏或调用点，避免公开语义暗示 allocator 会自动创建 perfect-fit cache。
- [x] 5.3 更新 spec 验证和源码级测试，禁止调用方假设 TODO 已实现。

## 6. 测试与 Runtime 验证

- [x] 6.1 更新源码级测试，覆盖 large allocation header、fallback 路径、空 slab 回收、perfect-fit 语义和 debug guard 开关。
- [x] 6.2 复用既有 memory runtime validation，扩展 self-test 覆盖 small object、large object、空 slab 回收和统计恢复。
- [x] 6.3 覆盖 `init_mem()` 后 runtime buddy split 仍使用普通 allocator 路径，确认 slab lifecycle 改动不会回退依赖 early metadata arena。
- [x] 6.4 运行 `uv run pytest tests/test_memory_correctness_source.py`，记录通过结果或无法运行原因。
- [x] 6.5 运行 `openspec validate --all`，确认 spec delta 可解析。

## 7. 构建、静态检查与收尾

- [x] 7.1 运行 `xmake`，确认交叉工具链构建通过；若缺失，记录缺失命令和风险。
- [x] 7.2 对修改过的 C++ 文件运行 freestanding x86_64 C++17 clang 辅助 `-fsyntax-only` 检查。
- [x] 7.3 使用 clangd 或 IDE diagnostics 检查修改过的文件，修复当前 change 引入的有效诊断。
- [x] 7.4 运行 `uv run python tools/boot_debug.py run --no-launch`，确认 boot 资产生成成功。
- [x] 7.5 在 Bochs 环境可用时运行 `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED` 或等价 bounded boot smoke，确认 slab lifecycle 路径不破坏启动。
- [x] 7.6 复查 `git diff`，确认未引入 scheduler/IRQ/SMP、direct map、用户态 heap 或完整 kmem_cache_create API。
- [x] 7.7 汇总验证记录，区分通过检查、无法运行检查、历史 warning、当前 change 新增或修复的诊断和剩余风险。


## 验证记录

- `uv run pytest tests/test_memory_correctness_source.py`：通过，21 passed，覆盖 large allocation header/fallback、空 slab 回收、perfect-fit 禁用语义、debug guard 开关和 runtime self-test 源码路径。
- `openspec validate --all`：通过，12 passed，change/spec delta 可解析。
- `xmake`：通过；保留既有 warning：`ISO C++11 requires whitespace after the macro name`、`build/kernel has a LOAD segment with RWX permissions`、`$(buildir) has been deprecated`。
- `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/mm/slab.cc src/mm/kmem.cc src/mm/self_test.cc`：通过。
- IDE diagnostics：`src/mm/slab.cc`、`src/mm/slab.h`、`src/mm/kmem.cc`、`src/mm/self_test.cc`、`include/bigos/memory.h` 均无诊断。
- `uv run python tools/boot_debug.py run --no-launch`：通过，生成 `build/test/os.raw` 和 `build/test/bochsrc.bxrc`。
- `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`：未通过，Bochs 启动后等待 serial marker 超时，且未生成 `build/test/serial.log`；运行后遗留的 `build/test/os.raw.lock` 已清理。该现象与既有历史验证记录一致，剩余风险是 runtime marker 未在本地 Bochs 环境中确认。
- `git diff` 复查：未引入 scheduler/IRQ/SMP allocator、direct map、用户态 heap、完整 `kmem_cache_create()` API，也未移动 `KVMEM_BASE`、self-mapping、boot/linker 地址或公开页分配 API。
