## 1. 开发

- [x] 1.1 梳理现有 file-backed read mapping、VMA、page fault、fork/COW、unmap/protection-change、teardown 与 frame_ref 调用路径，确认共享只读页的插入点和初始化顺序。
- [x] 1.2 为 VFS/backend 补齐稳定 object id：区分 backend/mount identity 与 backend-local object id，避免使用进程局部 `File` 指针作为跨 open 共享键。
- [x] 1.3 设计并实现有界共享只读页目录：定义 backing key、页偏移、只读属性、frame 引用、backing retain/release、独立容量上限、容量耗尽和失败回滚规则。
- [x] 1.4 将 file-backed read fault 改为共享目录 lookup/load/publish 流程：命中时复用 frame，未命中时经 page/buffer cache 装入、尾页 zero-fill、登记共享项并发布 read-only PTE。
- [x] 1.5 改造 ELF loader，使兼容的只读 text/rodata `PT_LOAD` segment 以 file-backed lazy VMA 表达，并在首次 fault 时进入共享只读目录；writable data/bss 不进入共享只读目录。
- [x] 1.6 更新 fork 路径，使 present shared read-only file-backed PTE 在 child 中引用同一 frame、递增引用计数，并在 fork 失败时完整回滚 child-side 共享引用。
- [x] 1.7 更新 explicit unmap、protection change、exec rollback/commit、exit/fault/reaper teardown 路径，使共享只读页按 PTE 和共享目录引用顺序释放，且不提前释放仍被其他进程引用的 frame。
- [x] 1.8 保证共享只读页的写访问和 writable/protection widening 请求确定性失败，不进入 COW、不创建 writable 私有副本、不写回 backing file 或 page/buffer cache。
- [x] 1.9 将共享只读页 PTE 发布、权限收紧、unmap 与 teardown 接入现有 TLB invalidation boundary，并保留当前单核 local invalidation 语义。
- [x] 1.10 标注共享目录和 frame_ref 更新的 SMP preparation 锁/内存顺序边界，确保普通 page fault 可阻塞上下文可更新，IRQ、调度临界区和 preemption-disabled 路径不装入共享页。

## 2. 输出

- [x] 2.1 增加默认关闭的共享只读映射 smoke 或等价行为验证入口，覆盖两个进程显式映射同一文件页、共享 frame、写访问失败、一个进程 unmap/exit 后另一个进程继续读取。
- [x] 2.2 扩展验证覆盖两个进程 `exec` 同一静态 ELF 后共享兼容 text/rodata 页，并确认 writable data/bss 保持私有。
- [x] 2.3 如新增用户态验证程序或 syscall wrapper flag，保持 append-only ABI 规则，更新最小用户态声明和构建打包逻辑，且不改变既有 syscall 号位。
- [x] 2.4 更新必要的 source-adjacent 注释或文档，说明共享只读页仍不提供 writable mapping、完整 POSIX `MAP_SHARED`、动态链接、共享库装载、真实 SMP 或 cross-CPU shootdown。
- [x] 2.5 实现完成并验证后，仅在能力实际落地时更新 `roadmap.md` 中对应任务 checkbox，保持 roadmap 为规划级描述，不加入入口点、命令、marker 或源码细节。

## 3. 回归

- [x] 3.1 运行 strict OpenSpec 校验，覆盖 `shared-readonly-mappings` change 及受影响 specs，并记录通过结果或工具缺失原因。
- [x] 3.2 运行最窄可用 `xmake` 交叉工具链构建；若 `x86_64-elf-gcc`、`x86_64-elf-g++` 或 xmake 不可用，记录 blocker 与剩余风险。
- [x] 3.3 针对改动过的 C++ 源码和头文件运行接近 freestanding x86_64 配置的 clang/clangd 辅助静态检查，区分历史诊断、当前 change 引入诊断和 freestanding 配置误报。
- [x] 3.4 在可用环境中运行共享只读映射默认关闭 smoke 的 QEMU headless 串口验证；若 QEMU、磁盘镜像、ROM、显示或串口 oracle 不可用，记录跳过原因和 bootability 风险。
- [x] 3.5 回归相关既有验证：file-backed read mapping、fork/COW、anonymous mapping lifecycle、userland 或 demand-paging smoke 中受影响的最小集合，确认既有 marker 和默认启动路径未被改变。
- [x] 3.6 审查 boot fixed addresses、higher-half base、direct-map window、`KVMEM_BASE`、recursive self-mapping、syscall vector、register ABI、exception/IRQ EOI 语义和磁盘布局均未移动或扩大。
