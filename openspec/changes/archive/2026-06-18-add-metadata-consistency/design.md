## Context

BigOS 当前已经具备单核 x86_64、有界用户态、VFS、RAM-backed `/rw`、persistent clean-sync `/rw`、目录树、metadata、受限 rename、稳定文件增长/截断、page/buffer cache 和最小用户态工具。现有 clean-sync 语义可以让同步后的数据跨 clean reboot 可见，但元数据持久化仍需要明确“哪些块必须一起提交、以什么顺序提交、失败后如何保持可解释状态”。

本设计影响文件系统和缓存路径，向上触及 fd/VFS、`fsync`/sync、目录树 mutation、metadata 查询和用户态验证；向下复用现有同步块 IO、page/buffer cache、RAM-backed 后端和 persistent clean-sync 后端。它不跨越 boot、IRQ、页表或 linker 边界，不改变 `int 0x80` 寄存器 ABI、既有 syscall 编号顺序、Legacy BIOS/MBR/exFAT boot image 布局或 UEFI backend parity。

## Goals / Non-Goals

**Goals:**

- 为 persistent `/rw` 定义有界 metadata commit 单元，覆盖目录项、inode、file size、block mapping、free-space metadata 和必要 volume metadata。
- 对 create、unlink、rmdir、rename、file growth、truncate 和 metadata update 建立 ordered write 提交顺序。
- 让成功 `fsync`、显式 sync 或受控同步完成后的元数据状态跨 clean reboot 可见。
- 同步失败、块 IO 失败、容量耗尽、cache block 耗尽或内核分配失败时，保留 dirty/pending state 或旧状态，不报告 durable success。
- 重新挂载时校验关键元数据一致性，拒绝明显不兼容或内部矛盾的持久卷。
- 增加 default-off 验证，覆盖提交顺序、失败语义、clean reboot 读回和非 crash-recovery 边界。

**Non-Goals:**

- 不实现完整 journal、journal replay、fsck、crash recovery、power-loss recovery 或未同步 dirty 状态持久化。
- 不实现完整 POSIX filesystem、完整目录 rename、atomic replacement、硬/软链接、mount namespace、ACL/xattr、完整时间戳或稳定 inode ABI。
- 不引入 async I/O、宽泛块层框架、新存储/设备 backend、SMP 启用、动态链接或完整 POSIX libc。
- 不修改 boot handoff ABI、MBR/exFAT 启动资产、页表布局、中断向量、syscall ABI 或只读 exFAT 状态。

## Decisions

### 1. 使用 bounded ordered writes，而不是 journal

元数据更新以一个有界 commit plan 表达：列出本次操作涉及的 data blocks、inode blocks、directory blocks、free-space metadata blocks 和 volume metadata blocks，并按操作类型规定写入顺序。成功完成全部必需写回后，`fsync` 或 sync 才能报告 durable success。

替代方案是引入最小 journal。journal 会改善 crash recovery 的表达力，但会增加 replay、checkpoint、空间保留和 mount-time recovery 复杂度，且当前 roadmap 明确不声明完整 crash recovery。因此本阶段采用 ordered writes，只保证 clean-sync 边界。

### 2. durable commit 以“先不可见/可回滚准备，后发布可见元数据”为原则

创建文件或目录时，先准备 inode、数据块和必要 free-space 状态，再写目录项发布可见性；删除或截断时，先保证新 inode/目录状态可解释，再释放不再拥有的块；rename 在有界范围内先准备目标父目录和源父目录更新，再以确定顺序提交目录项变化。

替代方案是每修改一个字段立即落盘。这会让失败路径产生孤儿 inode、丢失目录项或重复块所有权，不适合缺少 journal 的 persistent 后端。

### 3. free-space metadata 是块所有权的持久真源之一

任何数据块或 metadata block 在持久状态中只能由一个 live inode/metadata owner 持有，或由 free-space set 持有。释放块必须在旧 inode 映射不再持有后进入 free set；复用块必须在从 free set 移除并清零或完全覆盖后才能对用户可见。

替代方案是只依赖内存态引用计数。该方案无法在 clean reboot 后解释块所有权，也不能发现持久位图与 inode 映射之间的矛盾，因此不采用。

### 4. page/buffer cache 负责 dirty/pending 状态与写回错误传播

metadata commit 通过现有 page/buffer cache 获取、标脏、同步和淘汰。写回失败时，缓存必须保留 dirty 或等价 pending-write 状态，并把确定性错误传播到 `fsync`、sync 或触发写回的上层路径。不可阻塞上下文不得执行持久 metadata writeback。

替代方案是由 filesystem 直接绕过缓存写设备。该方案会分裂缓存可见性、淘汰和 fsync 语义，也会削弱后续统一 writeback 路径，因此不采用。

### 5. mount-time 只做有界一致性校验，不做 repair

重新挂载 persistent `/rw` 时，内核校验 superblock/format metadata、root metadata、inode bounds、directory entry bounds、block mapping 与 free-space metadata 的关键不变量。发现不兼容或明显内部矛盾时拒绝持久挂载或按既有策略降级，不自动修复、迁移或格式化。

替代方案是在 mount 时尝试修复。repair 需要更完整的 filesystem checker 和恢复策略，容易被误解为 crash recovery，因此不纳入本变更。

### 6. 验证以 clean reboot 和失败注入边界为核心

验证分为运行期状态检查、同步失败路径检查和双阶段 clean reboot 检查。若环境支持，可以通过测试后端或受控故障注入模拟 metadata block write failure；若不可用，验证记录必须明确说明无法覆盖的故障类别和残余风险。

## Risks / Trade-offs

- [Risk] ordered writes 不能覆盖断电或模拟器强停导致的中间状态。→ Mitigation: 规格和验证只声明 clean-sync 边界，明确不声明 crash recovery。
- [Risk] 元数据 commit plan 过宽会增加同步成本。→ Mitigation: 每个操作只列出必要 dirty metadata blocks，保持有界容量和同步路径。
- [Risk] 写入顺序实现错误可能产生块泄漏或重复所有权。→ Mitigation: 使用 mount-time 不变量检查和源码级/运行期验证覆盖 inode/free-space 对账。
- [Risk] cache 写回失败后，运行期已可见状态和 durable 状态可能不同。→ Mitigation: `fsync`/sync 失败不得报告 durable success，dirty/pending state 必须可解释。
- [Risk] 验证依赖持久测试盘和 emulator，可移植性有限。→ Mitigation: 验证记录区分 passed、skipped、blocked 和 residual risk，不把环境缺失报告为通过。

## Migration Plan

1. 审查 persistent `/rw` 当前 superblock、inode、directory、free-space metadata、file size、block mapping 和 cache dirty state 的提交顺序。
2. 定义 metadata commit plan 与各操作的 ordered write 顺序，先覆盖 create/unlink/rmdir/rename，再覆盖 growth/truncate。
3. 将 commit plan 接入 page/buffer cache 同步和错误传播路径，确保失败保留 dirty/pending 状态。
4. 增加 mount-time 有界一致性校验，拒绝明显不兼容或内部矛盾的持久卷。
5. 扩展 default-off 验证和必要用户态触发路径，覆盖 clean reboot 读回与失败语义。
6. 更新相关 docs/en 与 docs/zh 镜像文档时保持 bounded ordered writes 和非 crash-recovery 边界。

Rollback 策略是关闭新增验证和 ordered metadata commit 调用点，回退到既有 persistent clean-sync 行为；任何修改 boot layout、exFAT 启动资产、页表布局、IRQ/syscall ABI 或 syscall 编号重排的变更都应在 review 中阻断。

## Open Questions

当前无阻塞性待决问题。实现阶段可以在“每个操作显式构造 commit plan”和“由 dirty metadata 类型自动派生 commit plan”之间选择，但必须满足同一 ordered write、失败传播和 clean reboot 规格。
