## 1. 地址布局与 API 契约

- [x] 1.1 定义 direct-map 常量（如 `KDIRECT_BASE`、`KDIRECT_LEN`）并验证其与 `KVMEM_BASE`、recursive self-mapping、higher-half kernel base、boot fixed addresses 不重叠。
- [x] 1.2 在 `bigos::mm` 暴露最小 direct-map helper：范围判断、`phys_to_direct()`、`direct_to_phys()` 或等价命名，保持 public header 小而明确。
- [x] 1.3 明确 helper 对越界、未覆盖 RAM、MMIO/device address 返回显式失败值（如 `nullptr`、`false` 或 invalid sentinel），并保证不会返回 `KVMEM_BASE` 区域内的地址或触发普通探测路径的 panic。
- [x] 1.4 保留现有 allocator API 分层，不恢复 `alloc_pages()`、`alloc_physical_pages()`、`free_physical_pages()` 等旧 alias。

## 2. 页表映射与初始化

- [x] 2.1 从 BootInfo memory map 中选择 direct-map 覆盖的 page-aligned ordinary RAM ranges，包含 buddy 可分配 RAM 与物理上属于 ordinary RAM 的 kernel/boot 已消费区间，跳过 ACPI、firmware-owned、reserved、MMIO/framebuffer 或超出 `KDIRECT_LEN` 的范围。
- [x] 2.2 实现 direct-map 页表初始化路径，复用或抽出当前 recursive self-mapping 页表访问逻辑，写入 present、writable、supervisor-only leaf entries。
- [x] 2.3 处理 direct-map 页表页分配失败：要么回滚本次已写 descriptor/PTE，要么通过统一 `kpanic` 输出稳定 `BIGOS_` marker 后停机。
- [x] 2.4 审查初始化顺序，确认 direct map 不破坏 buddy early metadata arena、slab/kmalloc 初始化、`init_vmem()` 和 `BIGOS_MM_SELF_TEST` 的现有依赖。

## 3. 验证与自测覆盖

- [x] 3.1 扩展 source-level 测试，检查 direct-map window 与 `KVMEM_BASE`、self-mapping、higher-half kernel 不重叠，且 `KVMEM_BASE` 仍不被描述为 direct map。
- [x] 3.2 扩展 `BIGOS_MM_SELF_TEST` 或等价 gated runtime validation，验证至少一个受控 buddy-allocated RAM 页的 direct-map 转换可逆、读写可访问和释放后 allocator 统计恢复。
- [x] 3.3 为越界转换、非 RAM/MMIO 不覆盖、显式失败值策略补充可检查的源码级或 runtime 验证。
- [x] 3.4 确认新增测试不依赖 scheduler、IRQ enable、SMP、filesystem、用户态或 hosted runtime API。

## 4. 构建与静态检查

- [x] 4.1 运行最窄可用交叉构建（例如 `xmake`），若 `x86_64-elf-gcc`/`x86_64-elf-g++` 或 xmake 不可用，记录缺失依赖和剩余风险。
- [x] 4.2 运行适用的源码级测试（例如 `uv run pytest tests/test_memory_correctness_source.py` 或新增 direct-map source test），若 `uv` 不可用则记录 blocker，不静默改用 system Python。
- [x] 4.3 以接近 GCC cross-build 的 freestanding C++17、x86_64 target、no exceptions、no RTTI、项目 include paths 配置运行 clang 辅助检查；记录历史诊断、当前 change 新诊断和误报。
- [x] 4.4 运行 clangd 辅助诊断或记录 clangd 配置不可用原因；修复当前 change 引入的有效错误和警告。

## 5. Boot Smoke 与文档

- [x] 5.1 在 Bochs、ROM 路径和 disk image 环境可用时运行 bounded emulator smoke，例如 `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`。
  - 已通过 `uv run python tools/boot_debug.py run --bochs-extra "display_library: sdl2" --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`，串口捕获 `serial marker observed: BIGOS_MM_SELF_TEST_PASSED`。
- [x] 5.2 若 emulator smoke 不可运行，至少运行可用的 boot image/no-launch 资产生成路径，并记录未运行 Bochs 的原因、已覆盖检查和剩余 bootability 风险。
- [x] 5.3 更新内存布局文档或代码注释，说明 direct map、`KVMEM_BASE`、recursive self-mapping、higher-half kernel、low identity map 和未来 MMIO mapping 的边界。
- [x] 5.4 运行 `openspec validate define-kernel-direct-map --strict`，修复 proposal/design/spec/tasks 的格式或 requirement/scenario 问题。
