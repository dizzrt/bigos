## 1. 设计与接入点确认

- [x] 1.1 复查 `proposal.md`、`design.md` 和 spec delta，确认本 change 只增加内存 runtime 验证，不改变 allocator 策略。
- [x] 1.2 复查 `src/kernel/kernel.cc`、`src/mm` 和 boot debug 工具，确认 `init_mem()` 后、IRQ 初始化前的 self-test 接入点。
- [x] 1.3 确认本 change 不移动 boot 地址、linker 地址、self-mapping 地址、`KVMEM_BASE` 或 kernel load base。

## 2. Memory Self-Test 实现

- [x] 2.1 增加可开关的内存 self-test 编译配置，默认不改变普通 kernel boot 行为。
- [x] 2.2 在 `src/mm` 中实现 self-test 入口，覆盖 `kmalloc/free` 代表性 size class 分配、写入和释放。
- [x] 2.3 扩展 self-test 覆盖 `alloc_kernel_pages/free_pages` 的 1 页、多页和跨页表边界可写性检查。
- [x] 2.4 扩展 self-test 覆盖内部 `alloc_physical_order/free_physical_order` 的低 order 分配释放和 `g_nr_free_pages()` 恢复。
- [x] 2.5 为 self-test 成功和失败路径输出固定 marker，失败时输出阶段并安全 halt。

## 3. Emulator Smoke Oracle

- [x] 3.1 调研现有 `tools/boot_debug.py`、generated bochsrc 和 Bochs 输出路径，选择 VGA、串口或日志 marker 作为 bounded oracle。
- [x] 3.2 增加或调整 boot smoke 辅助路径，使其能在启用 self-test 的构建中等待固定 success marker。
- [x] 3.3 若本机无法运行 Bochs runtime smoke，记录缺失 ROM、配置、交互限制或 oracle 缺口。

## 4. 源码级测试与文档

- [x] 4.1 更新 `tests/test_memory_correctness_source.py` 或新增测试，断言 self-test 受编译开关控制且不默认运行。
- [x] 4.2 增加源码级测试，断言 self-test 不依赖 scheduler、IRQ enable、SMP 或 hosted APIs。
- [x] 4.3 在相邻文档或注释中记录 memory runtime validation 的启用方式和预期 marker。

## 5. 构建与静态检查

- [x] 5.1 运行 `uv run pytest tests/test_memory_correctness_source.py`，记录通过结果或无法运行原因。
- [x] 5.2 运行 `openspec validate --all`，确认 spec delta 可解析。
- [x] 5.3 运行 `xmake`，确认 `x86_64-elf-*` 交叉工具链构建通过；若缺失，记录缺失命令和风险。
- [x] 5.4 对修改过的 C++ 文件运行 freestanding x86_64 C++17 clang 辅助 `-fsyntax-only` 检查，记录历史诊断和当前 change 新增诊断。
- [x] 5.5 使用 clangd 或 IDE diagnostics 检查修改过的文件，修复当前 change 引入的有效诊断。

## 6. Runtime 验证与收尾

- [x] 6.1 运行 `uv run python tools/boot_debug.py run --no-launch`，确认 kernel build、boot build、raw image 和 generated bochsrc 生成成功。
- [x] 6.2 在 Bochs 环境可用时运行 bounded memory self-test boot smoke，确认 success marker 和既有 kernel reached marker。
- [x] 6.3 复查 `git diff`，确认未引入 scheduler/IRQ/SMP、direct map、页表页回收或 allocator 策略重构。
- [x] 6.4 汇总验证记录，区分通过检查、无法运行检查、历史 warning、当前 change 新增或修复的诊断和剩余 runtime 风险。
