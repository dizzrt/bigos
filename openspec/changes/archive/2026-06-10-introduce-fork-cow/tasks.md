## 1. mm 层：引用计数、COW 标记与按根重映射

- [x] 1.1 在 [memory.h](include/bigos/memory.h) 集中定义 `PTE_COW` 软件位常量（选 PTE bit9），并注释其与 present/writable/user/NX 硬件位正交、占用唯一性。
- [x] 1.2 在 `kernel/mm` 新增按物理帧号索引的引用计数表（直接映射区定长 `uint16_t` 数组，上界取最高可分配物理帧号），新增 `init_frame_refcount()` 并接在 [kmem.cc](kernel/mm/kmem.cc) `init_mem` 末尾、`init_direct_map` 之后；定义计数饱和为确定性失败（回滚 `fork`）而非回绕（决策 8）。
- [x] 1.3 实现并导出 `frame_ref_inc(phys)` 与 `frame_ref_dec_and_maybe_free(phys)`（归零才 `free_user_frame`），接口注释标注「单核 / 非 IRQ-context only」，与现有 mm 原语约定一致。
- [x] 1.4 新增 `remap_user_page_in_root(root, vaddr, phys, attr)`：覆盖已存在 PTE 的属性（含 invlpg 当前页），以及读取指定 root 下某用户 vaddr 叶子 PTE 物理帧/属性的访问器，供 COW 复制与写分裂使用。
- [x] 1.5 让 `teardown_user_address_space(root)` 对每个进程拥有的用户叶子帧改走 `frame_ref_dec_and_maybe_free`，保持借用高半区项「不释放」与 guard 页「不计数」规则；确认非 fork 进程帧初始计数为 1 时 teardown 等价于原 free 语义。

## 2. proc 层：fork、COW 地址空间复制与 fd 表复制

- [x] 2.1 在 [proc.h](include/bigos/proc.h) 新增 `int64_t fork_current() noexcept` 及内部 `clone_user_address_space_cow` / `clone_fd_table` / `cow_split_current` 声明与必要结构注释。
- [x] 2.2 实现 `clone_user_address_space_cow(parent, child)`：`derive_user_address_space_root` 派生子 root、`clone_process_kernel_stack_mapping` 建子内核栈、值复制 `VmaCollection`（含 `materialized_*` 与 heap/anon 记账）。
- [x] 2.3 在上一步基础上遍历每个 VMA 已物化页区间，按决策 9 分流：`VmaBacking::ElfSegment` 页（含可写 `.data`）一律分配新帧、复制内容、按段属性独立映射进子 root（不共享、不打 COW、初始计数 1）；`VmaBacking::Anonymous` 可写已物化页把父子两侧 PTE 重映射为只读 + `PTE_COW` 并 `frame_ref_inc` 共享父帧；`VmaBacking::Anonymous` 只读已物化页以只读属性映射进子 root 并 `frame_ref_inc`；未物化惰性区间仅复制元数据不分配帧；guard/借用高半区项跳过。
- [x] 2.4 实现 `clone_fd_table(parent, child)`：按父 `fd_capacity` 分配子 `fd_table`、值复制 `FdEntry` 并对每个非空 `file` 走 VFS retain，保证父子各自 `close` 互不影响、保持只读 VFS 语义、遵守 `MAX_FDS_SOFT_LIMIT`。
- [x] 2.5 实现 `fork_current()`：分配 PID 与 `Process` 对象、复制地址空间与 fd 表、把父 `InterruptFrame` 拷入子内核栈并改写子帧 `rax=0`、链接父子/兄弟链、发布进程并入调度，父返回子 PID；任一步失败按逆序回滚（ref--、teardown root、free fd_table/Process、归还 PID），父进程保持 Running 并返回确定性负 errno（`-ENOMEM` / `-EAGAIN`），达到进程软上限时确定性失败。
- [x] 2.6 在 [proc.cc](kernel/core/proc/proc.cc) 的 `try_handle_user_page_fault` 内、present 位判定处新增 COW 写分裂分支：写访问命中 present、只读、`PTE_COW`、可写匿名 VMA 时进入 `cow_split_current`；其余 present 违例维持原 `return false`(kill)。
- [x] 2.7 实现 `cow_split_current(page, vma)`：原帧 ref==1 时原地置 WRITABLE、清 `PTE_COW`、invlpg；ref>1 时分配新帧（失败 -> 经 fault-to-lifecycle 确定性 kill、不留部分映射、不损坏共享帧）、复制原帧内容、`remap_user_page_in_root` 把当前进程页重映射为可写、原帧 `frame_ref_dec_and_maybe_free`。

## 3. syscall 层：SYS_FORK

- [x] 3.1 在 [syscall.h](include/bigos/syscall.h) 的 `SyscallNumber` 末尾新增 `SYS_FORK`（紧随 `SYS_MAP_ANON = 9`），并补充其父返回子 PID / 子返回 0 / 失败负 errno 的注释。
- [x] 3.2 在 [syscall.cc](kernel/core/syscall/syscall.cc) 的 `dispatch` 新增 `SYS_FORK` 分支：校验非 IRQ / 可分配 / 有当前 Running 进程的上下文，调用 `proc::fork_current()` 并把返回值写回 `frame->rax`；该路径不发 i8259 EOI、不放宽其它 gate、不改寄存器约定。

## 4. 验证开关与 smoke

- [x] 4.1 在 [xmake.lua](xmake.lua) 新增默认关闭的 `fork_cow_smoke` 开关（`BIGOS_FORK_COW_SMOKE`），与现有 `demand_paging_smoke` / `growable_tables_smoke` / `user_*_smoke` 并列且互不删除。
- [x] 4.2 新增 `fork_cow_smoke_entry`（`#ifdef BIGOS_FORK_COW_SMOKE`，proc.h 声明 + proc.cc 实现），覆盖：fork 后父子可独立运行、写时分裂使父子内存隔离、父子先后退出时引用计数正确归还、分配失败确定性降级，并发射 `BIGOS_FORK_COW_PASSED` / `BIGOS_FORK_COW_FAILED`（COM1/VGA）。

## 5. C++ 辅助静态检查（clang/clangd）

- [x] 5.1 对新增/修改的 C++ 头与源（mm 引用计数、proc fork/COW、syscall 分支）运行 clang/clangd 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 目标、项目 include、无 hosted 运行时、无异常、无 RTTI）；若 clang/clangd 标志不可用则记录差距与残留风险。
- [x] 5.2 修复本次变更引入的 clang/clangd error，确认或修复新增有效 warning；验证记录区分历史诊断、本次变更诊断与工具链/freestanding 误报。

## 6. 构建、emulator 与 OpenSpec 验证

- [x] 6.1 运行最窄可用的 `xmake` 交叉工具链构建（确认 `x86_64-elf-gcc/g++/ld` 可用，否则显式记录缺失）；默认构建（无 smoke 开关）不应进入 `fork` 路径。
- [x] 6.2 运行 `fork_cow_smoke=y` 的 QEMU headless serial-marker smoke（经 `tools/boot_debug.py` 或 `xmake run qemu -- --display none`），断言 `BIGOS_FORK_COW_PASSED`；若 QEMU/Bochs/ROM/串口/镜像不可用则记录缺失依赖与残留 bootability 风险。
- [x] 6.3 运行相关 `uv run pytest` 源码契约/行为断言测试（沿用behavior assertion validation baseline 行为断言轨道，新增 fork/COW 行为断言用例）；`uv` 不可用则显式记录而非回退系统 Python。
- [x] 6.4 运行 `openspec validate introduce-fork-cow --strict` 并修正结构问题；确认引用计数表、`PTE_COW` 位、COW 缺页分支等源码契约断言到位。
- [x] 6.5 复核未改动 boot 固定地址、higher-half base、direct-map 窗口、`KVMEM_BASE`、递归自映射地址、syscall 向量、exception/IRQ EOI 语义；验证记录分列「通过 / 因故未运行+原因+残留风险 / 历史诊断 / 本次引入问题」。
