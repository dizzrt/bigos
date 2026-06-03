## Why

近期内存管理的 correctness、API 语义和 VMem 布局已经完成基础修复，但当前验证仍主要依赖源码级检查、构建和 boot asset 生成，缺少能在 emulator 中观察 `init_mem()` 后真实分配/释放行为的运行时证据。现在需要建立最小可重复的内存管理运行时验证，避免后续 allocator、driver、console 或 boot 迭代在没有回归信号的情况下破坏早期内核堆。

## What Changes

- 增加可开关的早期内存管理 runtime self-test 入口，用于验证 `init_mem()` 后 allocator 基础行为。
- 为 `kmalloc/free`、`alloc_kernel_pages/free_pages`、`alloc_physical_order/free_physical_order` 增加 boot 阶段 smoke 覆盖。
- 增加 allocator 统计与不变量验证，确认测试前后物理页计数、虚拟页计数和释放路径保持一致。
- 建立 bounded Bochs runtime smoke oracle，优先通过固定 VGA/串口/log marker 判断 self-test 与 kernel reachability。
- 保留源码级测试、`openspec validate --all`、`xmake` 和 `boot_debug.py --no-launch` 作为辅助验证。
- 不引入 scheduler、IRQ enable、SMP、direct map、页表页回收、用户态地址空间或新的 allocator 策略。

## Capabilities

### New Capabilities
- `kernel-memory-runtime-validation`: 覆盖早期内存管理 runtime self-test、emulator smoke oracle、验证记录和不变量检查要求。

### Modified Capabilities
- `kernel-memory-correctness`: 将现有内存正确性验证要求从源码级/构建级扩展到可观察的 boot runtime 验证。

## Impact

- 影响子系统：`src/mm` 内存管理、`src/kernel/kernel.cc` 早期初始化路径、`tools/boot_debug.py` 或相邻 boot 验证工具、`tests` 源码级/脚本级验证。
- 架构假设：x86_64 long mode、当前单核关中断早期 kernel 环境、既有 self-mapping 页表布局和 `KVMEM_BASE` 不变。
- 工具链假设：优先使用 `x86_64-elf-*` 交叉工具链、`xmake`、`uv run pytest`、Bochs；缺失 Bochs 或 ROM 时必须记录未运行 runtime smoke 的原因。
- API 影响：不改变 `kmalloc()`、`alloc_kernel_pages()`、`free_pages()`、`alloc_physical_order()` 的公开语义。
