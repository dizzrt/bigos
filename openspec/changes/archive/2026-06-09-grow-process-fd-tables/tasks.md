## 1. 头文件与数据结构（include/bigos/proc.h）

- [x] 1.1 引入软上限常量 `MAX_PROCESSES_SOFT_LIMIT = 1024`、`MAX_FDS_SOFT_LIMIT = 256`，移除/替换原 `MAX_PROCESSES`、`MAX_FDS` 的硬上限语义；保留 `WAIT_ANY`、`ROOT_PARENT_PID` 等不变。
- [x] 1.2 把 `Process` 的内联 `FdEntry fd_table[MAX_FDS]` 改为可增长 fd 存储字段（堆分配指针 + 容量/计数），保持 `FdEntry` 语义不变；保留对外 API 函数签名不变。
- [x] 1.3 在 `Process` 内嵌注册节点指针（`reg_next` / `reg_prev`）以支撑侵入式双向链表注册结构，节点随 `Process` 对象一起分配；不破坏既有 `Process` 字段布局假设，确认不引入异常/RTTI/hosted 依赖。

## 2. 进程注册与对象生命周期（kernel/core/proc/proc.cc）

- [x] 2.1 把 `g_process_table[MAX_PROCESSES]` 改为侵入式双向链表注册结构（全局链表头 + `Process` 内嵌节点），重写 `lookup_process` / `publish_process` / `unpublish_process` 以遍历/增删链表，并以 `MAX_PROCESSES_SOFT_LIMIT` 约束；保持父子链与 PID 语义不变。
- [x] 2.2 新增 `alloc_process_object()`（`kmalloc` + 清零）与 `free_process_object()`（`free`），定义分配失败返回 `nullptr` 的确定性降级。
- [x] 2.3 调整 `alloc_pid` 的循环上界以适配大量进程，保持跳过 `0`/`WAIT_ANY` 与去重语义不变。
- [x] 2.4 在 `reap_pending_processes` 末尾、`unpublish_process` 且确认无引用后释放进程对象内存；确保不产生悬垂指针。
- [x] 2.5 替换 `launch_init` 的 `static Process init_process` 为堆分配对象，并处理 PID-1 语义下分配失败的确定性降级（沿用 `launch_init_failed` panic 边界仅用于 init 缺失/非法，分配失败按既有失败约定处理）。

## 3. fd 表可增长（kernel/core/proc/proc.cc）

- [x] 3.1 实现 fd 存储的随进程分配与回收，`install_fd_current` 复用最低空位或按需增长，达到软上限或增长失败返回 `-EMFILE`。
- [x] 3.2 调整 `read_fd_current` / `close_fd_current` / `close_all_fds` / `close_on_exec_fds` 按当前容量遍历，保持 close-on-exec、引用释放各一次的语义不变。

## 4. 验证开关（xmake.lua + proc）

- [x] 4.1 在 `xmake.lua` 新增默认关闭开关 `growable_tables_smoke` -> `BIGOS_GROWABLE_TABLES_SMOKE`，与现有 smoke 开关风格一致。
- [x] 4.2 在 `proc.cc` 的 `#ifdef BIGOS_GROWABLE_TABLES_SMOKE` 下实现 smoke 入口，覆盖「超过 16 上限创建进程/fd」「回收后槽位与 PID 复用」「注入分配失败确定性降级」，并发射 `BIGOS_GROWABLE_TABLES_PASSED`/`BIGOS_GROWABLE_TABLES_FAILED`；保留现有 `user_*_smoke`/`demand_paging_smoke` 矩阵不删除。

## 5. C++ 静态检查与构建验证

- [x] 5.1 运行 `xmake`（x86_64-elf-gcc/g++）确认默认构建通过；若交叉工具链不可用则显式记录跳过原因与残留风险。
- [x] 5.2 运行 clang/clangd 辅助静态检查（freestanding C++17、x86_64 目标、项目 include、无 hosted/异常/RTTI），修复本变更引入的错误并确认新告警；若工具不可用则记录差距。区分历史诊断、本变更诊断与 freestanding 误报。
- [x] 5.3 内存/对象生命周期审查：进程对象与 fd 存储的分配阶段、对象释放时机、对齐、悬垂与失败行为；确认 `kmalloc`/`free` 不在 IRQ 上下文调用。

## 6. 运行时与回归验证

- [x] 6.1 默认构建 QEMU headless boot 冒烟，确认 `BIGOS_INIT_ENTER`/`BIGOS_INIT_EXIT` 等默认 marker 行为不回归（`uv run python tools/boot_debug.py run --emulator qemu --display none ...`）。
- [x] 6.2 启用 `growable_tables_smoke` 构建并在 QEMU headless 验证 `BIGOS_GROWABLE_TABLES_PASSED`；记录通过项、无法运行项（含原因与残留风险）。
- [x] 6.3 运行受影响的源码契约测试（`uv run pytest`，含 `test_process_lifecycle_source.py`、`test_fd_vfs_shell_source.py` 等），更新因 `MAX_PROCESSES`/`MAX_FDS`/`static Process` 改动而失效的断言，尽量向行为断言（marker）方向迁移。

## 7. 文档与一致性

- [x] 7.1 更新 `roadmap.md` growable process and fd table capability 状态（建议中 -> 进行中/完成），并在归档时补充 archived change 名称。
- [x] 7.2 同步受影响的 `docs/en` 文档并镜像到 `docs/zh`（fd-vfs-shell、进程模型相关页），使用仓库相对路径，不暗示 ABI/引导/页表变更。
- [x] 7.3 运行 `openspec validate grow-process-fd-tables --strict` 与 `openspec status`，确认 change 结构与 spec delta 可解析。
