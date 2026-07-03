## 1. 格式与布局

- [x] 1.1 梳理现有 bigfs superblock、inode table、bitmap、data region 与 persistent test disk 容量，将 journal 固定为 32 blocks，并确认最大单事务 block 数与现有 smoke 数据集仍可容纳
- [x] 1.2 提升 journal-capable bigfs format version，增加 journal bounds、sequence/checksum/clean state 元数据，并保证旧 clean-sync image 被确定性拒绝
- [x] 1.3 更新 explicit persistent format 路径，初始化 journal 区域、root metadata、allocation metadata，并在发布 `/rw` 前验证 journal-clean 状态
- [x] 1.4 保持 RAM-backed `/rw` 初始化与只读 exFAT boot asset 路径不变，确认默认启动策略不被 journal layout 改动

## 2. Journal Transaction Core

- [x] 2.1 新增 bounded journal record/header/commit/checkpoint 数据结构，覆盖 after-image block、home block number、record count、sequence 和 checksum
- [x] 2.2 实现 journal transaction builder，将 data block、directory block、inode block、bitmap/free-space block 和 superblock/sequence block 加入有界事务
- [x] 2.3 实现 journal-first commit 顺序：descriptor/payload records -> commit marker -> home-location blocks -> checkpoint/clear marker
- [x] 2.4 在 journal 写入、commit marker、home update 或 checkpoint 任一阶段失败时返回确定性错误，并保留 dirty/pending 状态用于重试或诊断
- [x] 2.5 在 mount validation 中识别 clean journal、partial uncommitted journal 和 committed uncheckpointed journal；M15.1 对 committed but not checkpointed 状态拒绝发布 persistent `/rw`，普通启动 fallback 到 RAM-backed `/rw` 时记录明确诊断

## 3. Cache Ordering

- [x] 3.1 为 page/buffer cache 增加 selected ordered flush helper 或等价接口，按调用方阶段顺序同步指定 `(device, block_no)` 集合
- [x] 3.2 确保 ordered flush 在 request-layer error、timeout、queue failure、device write failure 或不可阻塞上下文中返回确定性错误且不清 dirty
- [x] 3.3 防止 cache eviction 绕过 active journal transaction 直接写回受保护 home-location block；无法按 journal 顺序写回时确定性失败且不复用 slot
- [x] 3.4 确认 `sync_device()`/`sync_all()` 不扩大 journal transaction 的 durable success 范围，persistent 成功路径使用 selected/device-scoped 语义

## 4. BigFS Mutation 接入

- [x] 4.1 将 create、mkdir 和目录扩展路径接入 journal transaction，覆盖 inode bitmap、child inode、parent directory block、parent inode 和必要 data block
- [x] 4.2 将 file growth/write 路径接入 journal transaction，保证新 data block durable ordering 先于 inode size/block mapping home update
- [x] 4.3 将 truncate 路径接入 journal transaction，覆盖 tail zeroing、released block ownership、data bitmap 和 inode metadata
- [x] 4.4 将 unlink、rmdir 和 delayed inode release 路径接入 journal transaction，保证 directory entry removal、link count/free-space 更新和 stale-data 防护顺序正确
- [x] 4.5 将 restricted regular-file rename 和 `utimens`/metadata update 路径接入 journal transaction，覆盖跨父目录和同父目录的有界 metadata set
- [x] 4.6 更新 `fsync()` 和 persistent sync 路径，使 pending metadata/data commit 通过 journal-first path 完成后才返回 success
- [x] 4.7 更新 public/internal comments、backend naming 或 diagnostics，明确 persistent `/rw` 是 journaled write path 但不是 mount-time recovery

## 5. 验证与工具链

- [x] 5.1 增加默认关闭 journaling validation 开关和串口/VGA marker，覆盖 create/write/fsync、目录 mutation、truncate/unlink、rename 和 clean validation boundary
- [x] 5.2 增加失败路径验证：journal payload write failure、commit marker failure、home-location write failure、capacity exhaustion 和 eviction ordering violation 均不得报告 durable success
- [x] 5.3 增加格式验证：旧 clean-sync persistent image、invalid journal bounds、partial uncommitted journal、committed uncheckpointed journal 均按 M15.1 策略拒绝 persistent mount；如 fallback 到 RAM-backed `/rw`，必须验证诊断可见
- [x] 5.4 运行 xmake/x86_64-elf-gcc 的最小构建或记录工具链缺失；运行相关 default-off smoke 或记录 QEMU/Bochs、ROM/display、serial capture、disk image 缺失和残余风险
- [x] 5.5 对新增或修改的 C++ 源/头执行接近 freestanding C++17/x86_64 交叉构建配置的 clang 辅助检查，区分历史诊断、当前变更诊断和工具配置 false positive
- [x] 5.6 对新增或修改的 C++ 源/头执行 clangd 辅助静态检查或记录不可用原因，并修复当前变更新引入的有效诊断

## 6. 文档与 OpenSpec 收口

- [x] 6.1 更新相关 docs/en 与 docs/zh 文件，区分 RAM-backed runtime、persistent clean-sync 历史边界、M15.1 journaled write path 和 M15.2 recovery 后续边界
- [x] 6.2 更新 `roadmap.md` 中 M15.1 完成状态时只描述规划层结论，不加入实现路径、文件名、命令或 archive 索引
- [x] 6.3 运行 `openspec validate add-rw-write-ahead-journal --strict`，修复 proposal/design/spec/tasks 的结构和 requirement/scenario 问题
- [x] 6.4 汇总验证记录，明确已通过检查、未运行检查及原因、历史问题、当前变更风险和 recovery 未覆盖范围
