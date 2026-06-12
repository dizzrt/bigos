## 1. 修复前确认

- [x] 1.1 复查 `kernel/mm` 初始化顺序，确认本 change 不移动 boot/linker 地址、不引入 IRQ enable、scheduler 或 SMP 假设。
- [x] 1.2 记录当前 `kmalloc`、`alloc_pages`、`alloc_physical_pages` 的调用关系，标出页数与 buddy order 的传递点，并制定本 change 内的 API 拆分迁移清单。
- [x] 1.3 检查现有 `xmake`、`x86_64-elf-*`、Bochs 和 disk image 路径是否可用，记录不可用工具及剩余验证风险。

## 2. Buddy 物理页分配器

- [x] 2.1 修复 `Zone::merge()` 前驱合并分支，确保检查 `prev` 时读取前驱节点而不是后继节点。
- [x] 2.2 调整 buddy 合并流程，使合并后调用方不会继续访问已删除的 `PageBlock` 或 list node。
- [x] 2.3 校验 `__new_free()` 与 `free()` 的页数统计更新，保证 zone 统计和 `gNrFreePages/gNrPages` 不因合并发生漂移。
- [x] 2.4 校验 `Zone::alloc()` split 后剩余块的 base、len、order、zone、flags 与 free list 顺序一致。
- [x] 2.5 为物理页分配失败路径补充安全返回，避免链表或统计在部分修改后继续执行。

## 3. Slab 与 kmalloc

- [x] 3.1 修复 `kmem.cc` 中 `16B` 到 `2048B` 静态 slab/cache 的对象大小，使名称与 size class 一致。
- [x] 3.2 迁移 slab 动态扩容到拆分后的 kernel virtual pages 接口，确保传入页数为 `1 << buddy_order_`。
- [x] 3.3 确保 slab 动态扩容得到的 heap backing 已映射并可立即访问，普通 `kmalloc/new` 不暴露未映射地址。
- [x] 3.4 补齐 slab 扩容各阶段失败回滚，避免 heap、bitmap、`Slab` 或 list node 半初始化后进入 cache list。
- [x] 3.5 复查 `free(ptr)` 的 slab header 校验、位图 reset、full-to-available 状态迁移和 cache free object 计数。

## 4. VMem 与页表映射

- [x] 4.1 修复页表索引宏或 helper，确保 PML4/PDPT/PD/PT index 都使用 x86_64 9 位索引范围。
- [x] 4.2 拆分页数接口与 physical order 接口，新增或重命名为语义明确的 kernel virtual pages 分配入口和 physical buddy order 分配入口。
- [x] 4.3 迁移当前调用点，确保页数接口只接收 `nr_pages`，physical buddy 接口只接收 `order`。
- [x] 4.4 将 VMem 页数分配实现改为 first-fit，选择页数足够的 free block，而不是无条件使用 free list 头节点。
- [x] 4.5 在 VMem 分配和释放路径维护 `nr_free_pages_`，并保证 free/used list 状态与计数一致。
- [x] 4.6 修复 `VMem::merge()` 中前驱合并后的对象生命周期问题，避免删除 `mblk` 后继续访问。
- [x] 4.7 修复释放 kernel virtual pages 时 physical backing 和 `physical_area` 节点的生命周期，避免 iterator/node 使用顺序错误。
- [x] 4.8 为 kernel virtual pages 分配入口和 `set_paging()` 补充空指针与物理页分配失败处理，避免写入无效 present descriptor。

## 5. 测试与文档检查

- [x] 5.1 添加或整理最小 allocator 测试覆盖，覆盖 buddy split/merge、slab size class、VMem 分配释放、页表索引边界和页数/order API 使用边界。
- [x] 5.2 确认本 change 不新增 `debug_check_buddy()`、`debug_check_vmem()` 或等价 debug-only allocator 扫描函数。
- [x] 5.3 复查 `memdef.h`、`vmem.cc` 中地址布局常量，确认本 change 未改变 `0x2000` PML4、低地址保留、kernel load base 或 higher-half base。
- [x] 5.4 在必要位置补充简短注释，说明 `kmalloc/new` 返回地址必须可访问、页数/order API 已拆分，以及当前 allocator 只保证单核关中断路径。
- [x] 5.5 确认本 change 不主动大幅增加静态 slab 容量，也不引入 early allocator；若验证暴露容量不足，仅记录为后续 bootstrap 风险。

## 6. 验证

- [x] 6.1 运行窄范围构建检查，例如 `xmake`；如果 cross toolchain 缺失，记录缺失命令和无法验证的风险。
- [x] 6.2 对修改过的 C++ 源文件运行尽量接近 freestanding x86_64 C++17 的 clang 辅助静态检查；若 clang 配置不可用，记录原因。
- [x] 6.3 使用 clangd 或 IDE diagnostics 检查修改过的 C++ 文件，区分历史诊断、当前 change 新增诊断和 freestanding 误报。
- [x] 6.4 若 Bochs 和镜像配置可用，运行 kernel boot smoke test，确认 `init_mem()` 后 kernel 能继续到达既有启动输出。
- [x] 6.5 汇总验证记录，明确列出已通过检查、无法运行检查、剩余风险和后续 bootstrap/early allocator 建议。
