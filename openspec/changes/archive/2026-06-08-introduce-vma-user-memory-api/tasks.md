## 1. VMA 数据模型

- [x] 1.1 定义进程 VMA 条目、权限、用途、backing 类型、增长策略和有界容量常量，保持 public headers 最小化
- [x] 1.2 实现 VMA 集合初始化、插入、删除、查找、重叠检测、权限覆盖检查和容量耗尽错误路径
- [x] 1.3 为 VMA 操作补充源码级检查，覆盖非重叠插入、权限判定、范围溢出、低半区边界和容量耗尽
- [x] 1.4 审查 VMA 数据结构的 kmalloc/slab 使用、对象 lifetime、alignment、失败回滚和单核临界区语义

## 2. 地址空间映射集成

- [x] 2.1 将用户页 map helper 调整为 VMA-first 校验，确保 PTE user/writable/NX 属性不宽于 VMA 权限
- [x] 2.2 实现 VMA-backed user range validation helper，并在读取/写入用户 buffer 前同时检查 VMA 与页表可访问性
- [x] 2.3 保持 boot 固定地址、higher-half、direct map、`KVMEM_BASE`、recursive self-mapping、syscall vector 和 IRQ/exception EOI 语义不变
- [x] 2.4 为无 VMA 映射、present PTE 但 VMA 不允许、VMA 允许但页缺失等场景补充源码级检查

## 3. 进程与 exec 生命周期

- [x] 3.1 扩展进程对象，使 committed image 与 staging image 能分别持有 VMA 集合
- [x] 3.2 在 ELF exec staging 中为代码段、数据/BSS、初始 stack、stack guard/growth 和 heap 边界创建 VMA
- [x] 3.3 实现 exec commit/rollback 的 VMA 发布与释放，保证 commit 前失败不破坏旧镜像
- [x] 3.4 将 exit、user fault、zombie、wait 和 safe reaper 路径接入 VMA cleanup，避免在活动 CR3/内核栈路径释放当前 VMA

## 4. 用户内存 API

- [x] 4.1 实现最小 `brk` 内核 API，覆盖 heap 扩展、收缩、边界检查、页分配/释放和失败回滚
- [x] 4.2 实现受限 anonymous mapping API，拒绝 file-backed、shared、W+X、kernel-space、overlap 和 unsupported flags
- [x] 4.3 将 `brk` 与 anonymous mapping 暴露到 `int 0x80` syscall dispatch，保持现有寄存器 ABI 与 no-EOI 规则
- [x] 4.4 更新 syscall user-buffer 校验路径，使 `write`、`open`、`read` 等用户 pointer 使用 VMA-backed validation

## 5. 用户栈增长

- [x] 5.1 定义用户 stack guard、最大增长范围、当前 materialized stack 边界和 VMA 元数据更新规则
- [x] 5.2 在 `#PF`/user fault 路径增加严格 stack-growth gate，仅允许 CPL3、栈 VMA 命中、权限匹配且上下文可分配时恢复
- [x] 5.3 保持 CPL0 page fault 诊断语义和非 stack 用户 fault 终止语义，不引入通用 demand paging
- [x] 5.4 为 stack-growth 成功、guard 命中、越界、权限不匹配、不可分配上下文和 CPL0 fault 补充验证

## 6. 构建与静态验证

- [x] 6.1 运行或记录 `xmake` cross-toolchain build，确认 `x86_64-elf-gcc/g++`、xmake 和相关 binutils 可用性
- [x] 6.2 运行相关源码级检查；如使用 Python 辅助测试，命令必须通过 `uv run pytest`，若 `uv` 不可用则记录 blocker
- [x] 6.3 对新增/修改的 C++ 源码和 headers 执行 clang 辅助检查，使用尽量接近 freestanding C++17、x86_64、no exceptions、no RTTI 的 flags；记录 clang 不可用或 false positive
- [x] 6.4 对新增/修改的 C++ 源码和 headers 执行 clangd 辅助诊断，区分历史诊断、当前 change 引入诊断和 freestanding 配置误报

## 7. 运行时与 OpenSpec 验证

- [x] 7.1 运行 `openspec validate introduce-vma-user-memory-api --strict` 并修复当前 change 的 spec/task 结构问题
- [x] 7.2 运行最窄有用的 QEMU headless serial-marker smoke，覆盖 `brk`、anonymous mapping、VMA user-copy 或 stack-growth 中至少一个可观察路径
- [x] 7.3 对涉及 page table、`#PF`、syscall 或 port/IRQ 边界的改动，按可用性执行 Bochs 或 QEMU+Bochs 交叉验证
- [x] 7.4 记录 validation notes：已通过检查、因 QEMU/Bochs/ROM/display/cross-binutils/serial oracle/image path 不可用而跳过的检查、替代验证和剩余 bootability 风险
