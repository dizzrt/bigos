## 1. 开发：VMA range lifecycle helper

- [x] 1.1 审查现有 VMA、anonymous mapping、demand paging、fork/COW、file-backed read mapping 和 address-space teardown 调用点，确认当前初始化顺序与可复用 helper 边界
- [x] 1.2 实现页对齐、长度非零、用户低半区、range overflow、完整 VMA 覆盖、compatible anonymous/private backing 和 unsupported permission 的统一校验 helper
- [x] 1.3 实现 VMA range staging 逻辑，支持完整覆盖、前缀、后缀和中间区间拆分，并在 VMA slot 容量不足时保持旧集合不变
- [x] 1.4 实现 materialization accounting 更新 helper，覆盖未物化区间删除、已物化区间截断、拆分后左右区间继承和 protection change 保留物化状态
- [x] 1.5 审查新增 VMA helper 的 kmalloc/slab 使用、对象 lifetime、alignment、失败回滚和单核临界区语义

## 2. 开发：unmap 与 protection change

- [x] 2.1 实现匿名 unmap 内核路径，清除目标范围 leaf PTE，释放 owned frame 或递减 COW/shared frame 引用，并删除或拆分 VMA
- [x] 2.2 将匿名 unmap 接入现有动态页表 ownership 回收逻辑，确保空 PT/PD/PDPT 页可回收且 static kernel page table 不被释放
- [x] 2.3 实现 anonymous/private protection-change 内核路径，更新 VMA 权限并同步 present PTE 权限，拒绝 W+X、file-backed writable upgrade 和不兼容 backing
- [x] 2.4 将 unmap 与 protection change 的 PTE 清除或权限收紧接入现有 TLB invalidation 准备边界，当前单核路径必须 invalidation 受影响页
- [x] 2.5 审查 unmap/protection change 与 demand paging、COW fault、exec replacement、exit/reaper teardown、user-copy validation 的一致性

## 3. 开发：syscall 与用户态输出

- [x] 3.1 扩展 syscall number、公共 syscall header 和 `int 0x80` dispatch，新增 bounded anonymous unmap 与 protection-change syscall，保持寄存器 ABI 与 no-EOI 规则不变
- [x] 3.2 在 syscall path 中验证当前进程状态、普通 syscall 上下文、参数范围、权限 flags 和错误返回，禁止 IRQ/调度临界区等 unsafe context 执行地址空间修改
- [x] 3.3 为 freestanding userland 增加最小 wrapper，文档化 bounded BigOS 语义，不声明完整 POSIX `munmap`/`mprotect`
- [x] 3.4 增加小型用户程序或默认关闭 smoke 消费者，覆盖 map、unmap、protect、access-after-unmap 和 write-after-readonly 的可观察路径
- [x] 3.5 确认 `user-init-elf` 依赖和用户程序打包路径不会产生 stale user binary，并保持默认 boot path 不依赖新 smoke

## 4. 输出：规格、文档与验证记录

- [x] 4.1 更新必要的 syscall/user memory 文档或 source-adjacent notes；若修改 `docs/en`，同步更新 `docs/zh` 对应路径且保持语言事实一致
- [x] 4.2 记录 boot/address-layout 审查结果，确认 fixed boot addresses、higher-half base、direct map、`KVMEM_BASE`、recursive self-mapping、syscall vector、exception/IRQ gate 和 EOI 语义未被移动或放宽
- [x] 4.3 记录 validation notes，区分已通过检查、无法运行检查及原因、替代验证、历史诊断、当前 change 引入的问题和剩余 bootability 风险
- [x] 4.4 如实现过程中新增或修改 Python helper，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 验证；若 `uv` 不可用则明确记录 blocker

## 5. 回归：构建、静态检查与运行验证

- [x] 5.1 运行 `openspec validate complete-anonymous-mapping-lifecycle --strict` 并修复当前 change 的 proposal/design/spec/tasks 结构问题
- [x] 5.2 运行最窄有用的 `xmake` cross-toolchain build，确认 `x86_64-elf-gcc/g++`、xmake 和相关 binutils 可用；若不可用，记录缺失工具和剩余风险
- [x] 5.3 对新增/修改的 C++ 源码和 headers 执行 clang 辅助检查，flags 尽量贴近 freestanding C++17、x86_64、no exceptions、no RTTI；修复当前 change 引入的有效诊断
- [x] 5.4 对新增/修改的 C++ 源码和 headers 执行 clangd 辅助诊断，区分历史诊断、当前 change 诊断和 freestanding 配置误报
- [x] 5.5 运行源码级检查或 targeted tests，覆盖 VMA 拆分、容量耗尽 rollback、unmap frame/COW 引用释放、permission rejection、TLB invalidation 调用点和 materialization accounting
- [x] 5.6 运行 QEMU headless serial-marker smoke，覆盖 anonymous lifecycle 成功与失败路径；若 QEMU、serial oracle、disk image 或 cross-toolchain 不可用，记录跳过原因、替代检查和剩余 bootability 风险
- [x] 5.7 对页表、`#PF`、syscall 或低层地址空间修改按可用性执行 Bochs 或 QEMU+Bochs 交叉验证；若 Bochs ROM/display/image path 不可用，记录阻塞原因
