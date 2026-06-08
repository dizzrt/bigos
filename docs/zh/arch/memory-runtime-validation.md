# 内存运行时验证

BigOS 可以在 `init_mem()` 之后、IRQ/PIC 设置之前运行一个可选的早期内存运行时自检。
该自检仅面向模拟器验证构建，默认关闭。

## 启用方式

- 使用 `xmake f --mm_self_test=y` 配置，然后运行 `xmake` 或本地 emulator target，例如 `xmake run qemu`。
- 如需只生成并校验 raw image 而不启动 emulator，运行 `uv run python tools/boot_debug.py run --no-launch --serial-log build/test/serial.log`。
- 如需运行有界的自动化 smoke validation，优先使用 QEMU headless helper 路径：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`。
- 如需 Bochs 交叉验证，在本机 Bochs ROM/display 配置可用时运行 `uv run python tools/boot_debug.py run --emulator bochs --serial-log build/test/bochs.serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`。

## 标记

- 成功标记：`BIGOS_MM_SELF_TEST_PASSED`
- 失败标记：`BIGOS_MM_SELF_TEST_FAILED stage=<stage>`

成功和失败标记会写入 COM1 与 VGA。失败时会通过 `hlt` 安全地暂停 CPU。

## 覆盖范围

该自检覆盖代表性的 `kmalloc/free` 尺寸类别、已映射的内核虚拟页分配、直接的低阶物理 buddy 分配，以及一个受控 buddy page 的 kernel direct-map 转换与读写访问。direct-map 检查会确认 `phys_to_direct()` / `direct_to_phys()` 可逆、越界物理地址返回显式失败值、`KVMEM_BASE` 不会被当作 direct-map 地址，并在释放物理页后恢复 buddy 统计。它不会启用 IRQ、调度器、SMP、文件系统服务、用户态或宿主运行时 API。

## 中断上下文契约

阶段 3 明确普通 allocator 入口的上下文边界，但不把它们升级为 IRQ handler safe：

- `kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()` 和全局 `new/delete` 是 non-IRQ-handler-safe API。
- `alloc_kernel_pages()` 保持页数语义；内部 buddy `alloc_physical_order()` 保持 order 语义，不新增 `alloc_pages()`、`alloc_physical_pages()` 或 `free_physical_pages()` alias。
- `collect_slab_stats()`、`g_nr_free_pages()` 和 `kernel_vmem_free_pages()` 是 IRQ-disabled-only snapshot；它们会短暂屏蔽同 CPU maskable IRQ 交错以读取 allocator 统计。
- `print_slab_stats()` 和 `print_physical_memory_info()` 是 non-interrupt-context-only diagnostic output helper，不应从 IRQ handler 调用。

新增的 `bigos::irq::InterruptGuard` 保存进入前 RFLAGS.IF，进入时执行 `cli`，退出时仅在进入前 IF=1 时执行 `sti`。该 guard 只保护单核同 CPU maskable IRQ interleaving，不提供 SMP 互斥、NMI 保护、阻塞语义或 scheduler lock 语义。

allocator 内部只在 buddy free list/`PageBlock` accounting、slab cache list/bitmap/large-allocation accounting、KVMEM free/used list、physical backing record、PTE 写入/清除和 TLB invalidation bookkeeping 等元数据边界使用 guard。guarded region 不包含 `mdelay()`、文件系统、scheduler、用户态交互或批量 console/serial 输出；未来引入 scheduler/SMP 前仍需重新设计 spinlock、preemption 和 sleepable allocation 边界。

## 验证边界

源码级验证覆盖 API 标注、interrupt guard IF 保存/恢复、ISR allocator 禁止 token 和 `mm_self_test()` 初始化顺序。交叉构建或 `-fsyntax-only` 检查用于确认 freestanding C++/assembly 改动可由目标配置解析。

QEMU headless runtime smoke 是当前 Legacy BIOS/MBR/exFAT image 的首选自动化 serial-marker 路径。Bochs 仍适合早期 boot、BIOS、ATA PIO、中断和硬件行为差异交叉验证。若 QEMU、Bochs、cross-binutils、ROM/display 配置、disk image path 或 serial-marker 观测不可用，validation 需要记录未运行原因、已通过的替代检查和剩余 bootability 风险。
