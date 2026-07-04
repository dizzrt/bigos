## 1. Journal Recovery 状态分类

- [x] 1.1 梳理现有 bigfs v3 superblock、32-block journal 区、record/header/commit/checkpoint 字段，确认 M15.2 不需要磁盘布局扩容或版本迁移
- [x] 1.2 实现 journal scan/status classifier，区分 clean、partial、committed、checkpointed 和 corrupt/unsupported 状态
- [x] 1.3 为 classifier 增加 sequence、checksum、record count、payload length、target block bounds 和 journal-control block 校验
- [x] 1.4 确保旧格式、invalid journal bounds、目标 block 指向 journal 区或越界等状态确定性拒绝 persistent writable `/rw`

## 2. Replay 与 Discard 核心

- [x] 2.1 实现 committed transaction replay，将每个 after-image record 写回对应 home-location block，并保持 replay 幂等
- [x] 2.2 实现 replay 阶段的 ordered sync：home-location blocks 全部同步成功后，才写入 checkpoint/clear marker
- [x] 2.3 实现 uncommitted partial transaction 的安全 discard/clear，且不得把 partial records 当作成功 mutation replay
- [x] 2.4 在 replay、discard、checkpoint 或 clear 任一 I/O 失败时 fail closed，保留可重试或可诊断的 journal 状态
- [x] 2.5 确认 recovery 不改动只读 exFAT boot asset、BootInfo v2、syscall ABI、页表布局、默认 RAM-backed `/rw` 行为或 32-block journal 大小

## 3. Mount 集成与诊断

- [x] 3.1 将 recovery 阶段接入 persistent bigfs mount validation，在 VFS 发布 persistent writable `/rw` 前完成状态分类和必要恢复
- [x] 3.2 更新 persistent `/rw` 发布策略：clean、replayed、discarded 可发布；corrupt、unsupported、recovery I/O failure 必须拒绝发布
- [x] 3.3 保留普通启动 RAM-backed fallback 策略，并为 journal-needs-recovery、journal-recovery-failed、corrupt-journal 等路径输出稳定诊断
- [x] 3.4 确保 M15.1-only 或 recovery 未启用配置仍不声明 mount-time replay/discard/recovery pass
- [x] 3.5 将 journal-first write ordering 验证结果与 mount-time recovery 验证结果分开记录，避免成功写入路径掩盖恢复失败

## 4. 受控镜像与测试注入

- [x] 4.1 增加或扩展 persistent bigfs 测试镜像构造能力，能生成 clean journal、partial journal、committed-uncheckpointed journal 和 corrupt journal 状态
- [x] 4.2 为 committed replay case 构造包含文件内容、目录项、inode metadata、bitmap/free-space 更新的 after-image transaction
- [x] 4.3 为 repeated replay case 构造 replay 中途失败后再次 mount 的状态，验证重复 replay 不产生重复目录项或重复 block ownership
- [x] 4.4 为 partial discard case 构造无 commit marker 的 parseable journal，验证 mount 后 partial mutation 不可见且 journal 被清理
- [x] 4.5 为 corrupt/reject case 构造 checksum、sequence、record count、bounds 和 payload length 异常，验证拒绝 persistent writable `/rw`

## 5. Runtime Validation

- [x] 5.1 增加默认关闭 journal recovery validation 开关和稳定串口/VGA marker，覆盖 clean mount、committed replay、partial discard、corrupt reject 和 recovery failure fallback
- [x] 5.2 验证 successful replay 后文件内容、目录枚举、metadata 查询、fd I/O 和 allocation consistency 均可观察
- [x] 5.3 验证 recovery I/O failure 不发布 persistent writable `/rw`，并保留下一次 mount 可重试或可诊断的 journal 状态
- [x] 5.4 运行相关 default-off smoke；如 QEMU/Bochs、ROM/display、serial capture 或 persistent disk image 不可用，记录缺失项与残余风险

## 6. 构建与静态检查

- [x] 6.1 运行 xmake/x86_64-elf-gcc 的最小构建；如交叉工具链或 xmake 不可用，记录 blocker 和残余风险
- [x] 6.2 对新增或修改的 C++ 源/头执行接近 freestanding C++17/x86_64 交叉构建配置的 clang 辅助检查，区分历史诊断、当前变更诊断和工具配置 false positive
- [x] 6.3 对新增或修改的 C++ 源/头执行 clangd 辅助静态检查或记录不可用原因，并修复当前变更新引入的有效诊断
- [x] 6.4 如果实现过程中修改 Python helper 或测试脚本，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest` 验证；若未修改 Python 文件则记录不适用

## 7. 文档与 OpenSpec 收口

- [x] 7.1 更新相关 docs/en 与 docs/zh，说明 M15.2 recovery 后 clean、replayed、discarded、rejected 和 fallback 语义，并保持双语文档同步
- [x] 7.2 更新 `roadmap.md` 的 M15.2 状态时只描述规划层结论，不加入实现路径、文件名、命令或 archive 索引
- [x] 7.3 运行 `openspec validate add-journal-mount-recovery --strict`，修复 proposal、design、specs、tasks 的结构和 requirement/scenario 问题
- [x] 7.4 汇总验证记录，明确已通过检查、未运行检查及原因、历史诊断、当前变更风险，以及仍未覆盖的完整 POSIX power-loss 语义
