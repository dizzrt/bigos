# 内存运行时验证

BigOS 可以在 `init_mem()` 之后、IRQ/PIC 设置之前运行一个可选的早期内存运行时自检。
该自检仅面向模拟器验证构建，默认关闭。

## 启用方式

- 使用 `xmake f --mm_self_test=y` 配置，然后运行 `xmake` 构建。
- 或使用 `uv run python tools/boot_debug.py run --memory-self-test --no-launch` 构建启动资产，并生成一个将 COM1 路由到 `build/test/serial.log` 的 Bochs 配置。
- 如需运行有界的 Bochs 冒烟验证，执行 `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`。

## 标记

- 成功标记：`BIGOS_MM_SELF_TEST_PASSED`
- 失败标记：`BIGOS_MM_SELF_TEST_FAILED stage=<stage>`

成功和失败标记会写入 COM1 与 VGA。失败时会通过 `hlt` 安全地暂停 CPU。

## 覆盖范围

该自检覆盖代表性的 `kmalloc/free` 尺寸类别、已映射的内核虚拟页分配、直接的低阶物理 buddy 分配，以及一个受控 buddy page 的 kernel direct-map 转换与读写访问。direct-map 检查会确认 `phys_to_direct()` / `direct_to_phys()` 可逆、越界物理地址返回显式失败值、`KVMEM_BASE` 不会被当作 direct-map 地址，并在释放物理页后恢复 buddy 统计。它不会启用 IRQ、调度器、SMP、文件系统服务、用户态或宿主运行时 API。
