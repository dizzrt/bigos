## 1. 现状审查与提交边界确认

- [x] 1.1 审查 persistent `/rw` 的 superblock、inode、directory entry、free-space metadata、file size、block mapping 和 page/buffer cache dirty state 当前提交顺序
- [x] 1.2 审查 create、unlink、rmdir、受限 rename、file growth、truncate、`fsync` 和 explicit sync 的现有调用链，标出需要纳入 metadata commit unit 的状态
- [x] 1.3 确认当前 x86_64 Legacy BIOS 持久测试盘、RAM-backed fallback、只读 exFAT 隔离和 normal boot 初始化顺序不需要 boot/layout/ABI 改动
- [x] 1.4 记录本变更的非目标边界：不实现 journal replay、fsck、crash recovery、power-loss recovery、async I/O、新块层框架或完整 POSIX filesystem

## 2. metadata commit plan 与 page/buffer cache 对接

- [x] 2.1 设计并实现有界 metadata commit plan，覆盖 dirty data blocks、inode blocks、directory blocks、free-space metadata blocks 和必要 volume metadata blocks
- [x] 2.2 为 page/buffer cache 增加或复用 selected-block writeback 能力，使 filesystem 能按 commit plan 的顺序同步 metadata dirty blocks
- [x] 2.3 确保 metadata writeback 只在允许阻塞、分配和同步块 IO 的进程上下文执行，不从 IRQ、调度临界区或 preemption-disabled 路径发起持久 IO
- [x] 2.4 实现写回失败传播：块 IO 失败、cache block 不足或内核分配失败时返回确定性错误，并保留 dirty 或 pending-write 状态
- [x] 2.5 覆盖 cache eviction 遇到 dirty persistent metadata block 的行为，确保不绕过 ordered commit 或错误地清除 dirty state

## 3. 目录树 metadata 有序提交

- [x] 3.1 将 persistent `/rw` 文件和目录创建接入 ordered metadata commit，确保新 inode 和必要初始化块先于父目录项 durable publish
- [x] 3.2 将 unlink 和 rmdir 接入 ordered metadata commit，确保目录项移除、引用状态和释放块 metadata 的持久顺序可解释
- [x] 3.3 将受限常规文件 rename 接入 ordered metadata commit，覆盖同父目录 no-op、跨父目录常规文件移动和目标已存在保守失败路径
- [x] 3.4 覆盖目录树 mutation 失败回滚或 pending state，避免半成品目录项、孤儿 live inode、重复块所有权和只读 exFAT 状态污染

## 4. 文件增长、截断与 free-space metadata

- [x] 4.1 将 persistent `/rw` 文件增长接入 metadata commit，确保数据初始化和块映射先于 durable size 发布
- [x] 4.2 将收缩截断接入 metadata commit，确保旧 inode 引用移除先于释放块被记录为 durable free
- [x] 4.3 将扩展截断和 seek-past-EOF 零读范围接入 metadata commit，确保 clean reboot 后 size、zero gap 和 block ownership 一致
- [x] 4.4 实现或强化 free-space metadata 对账，防止同一块同时被 live inode mapping 和 free-space set 持有
- [x] 4.5 覆盖容量耗尽、cache block 耗尽、内核分配失败、用户缓冲失败和块 IO 失败路径，确保不发布 durable partial size、partial mapping 或 dirty-cache success

## 5. 重新挂载校验与失败策略

- [x] 5.1 增加 mount-time 有界一致性校验，覆盖 format metadata、root metadata、inode bounds、directory entry bounds、block mapping bounds 和 free-space ownership
- [x] 5.2 对不兼容 metadata、非法 inode 引用、目录项越界、块所有权冲突和 free-space 矛盾返回确定性拒绝或既有降级策略
- [x] 5.3 确保 mount-time 校验不自动 repair、auto-format、auto-migrate，不修改只读 exFAT boot assets，也不在正常初始化失败时 panic

## 6. 用户态触发路径与文档同步

- [x] 6.1 按最小 bounded userland 范围扩展 shell、小型用户程序或 default-off 验证入口，用于触发 create/unlink/rmdir/rename/growth/truncate/fsync 组合
- [x] 6.2 若新增或调整 libc wrapper，保持 freestanding-safe、错误映射有界，并避免声明完整 POSIX 兼容
- [x] 6.3 更新 docs/en 与 docs/zh 对应文档，说明 persistent `/rw` metadata ordered writes、clean-sync 边界、同步失败语义和非 crash-recovery 边界
- [x] 6.4 若文档或验证记录涉及 roadmap，只使用能力描述，不引用 roadmap 任务编号或阶段编号

## 7. 验证与质量检查

- [x] 7.1 增加或扩展源码级测试，覆盖 commit plan 构造、ordered metadata block 顺序、free-space/inode 对账、容量边界和失败不发布 durable success
- [x] 7.2 运行针对文件系统与验证脚本的 `uv run pytest ...`；若 `uv`、测试依赖或本地环境不可用，记录 blocker、跳过原因和残余风险
- [x] 7.3 运行 `xmake` 或等价 x86_64-elf GCC 交叉构建，确认 freestanding kernel build 通过；若 cross toolchain 不可用，记录缺失工具和残余风险
- [x] 7.4 对新增或修改的 C++ 源/头执行尽可能贴近 freestanding C++17/x86_64 交叉环境的 clang 和 clangd 辅助检查，区分历史诊断、当前变更诊断和工具配置误报
- [x] 7.5 运行 RAM-backed `/rw` default-off smoke，验证运行期 metadata mutation、失败回滚和只读 exFAT 隔离；记录 emulator/toolchain/ROM/display 可用性
- [x] 7.6 运行 persistent clean-sync 双阶段 smoke，验证同步后的目录项、inode metadata、file size、block mapping 和 free-space effects 跨 clean reboot 可见
- [x] 7.7 如环境支持，运行受控 metadata write failure 或 corrupted metadata mount 拒绝验证；若不支持，记录未覆盖故障类别和残余风险
- [x] 7.8 运行 `openspec validate add-metadata-consistency --strict`，修正 proposal/design/spec/tasks 格式或契约问题

## Validation Notes

- `uv run pytest tests/test_metadata_consistency_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_runtime_filesystem_maturity_source.py tests/test_stable_file_growth_source.py tests/test_bilingual_docs_layout.py` passed: 32 tests.
- `xmake` passed for the freestanding kernel build. Apple clang 21.0.0 is available on the host, but it is not the target x86_64 freestanding cross environment; the cross build is the authoritative C++ check here.
- `openspec validate add-metadata-consistency --strict` passed.
- RAM-backed `/rw` QEMU smoke passed with `BIGOS_WRITABLE_FS_SMOKE`, `--display none`, serial log `build/test/writable-fs-clean-serial.log`, and marker `BIGOS_WRITABLE_FS_PASSED`.
- Persistent clean-sync QEMU two-stage smoke passed with `BIGOS_PERSISTENT_WRITABLE_FS_SMOKE`, fresh `build/test/persistent-rw.raw`, serial logs `build/test/persistent-rw-write-clean.log` and `build/test/persistent-rw-verify-clean.log`, and markers `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED` / `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`.
- Corrupted metadata mount rejection validation passed: after seeding `build/test/persistent-rw.raw` with a valid persistent smoke write, byte offset `1055` (BigFS data bitmap block 2, byte 31, data-block index 248) was overwritten with `0x01` to mark an unowned high data block allocated. The first corrupted-image run exposed stale bcache state after persistent validation rejection, so `publish_persistent_if_valid()` now invalidates the persistent device cache before RAM fallback. Re-running the corrupted image produced `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED` in `build/test/persistent-rw-77-corrupt-fixed.log`, proving the inconsistent persistent volume was not published writable and the documented fallback/format path ran. A follow-up clean reboot verify run produced `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED` in `build/test/persistent-rw-77-post-reformat-verify.log`.
- Controlled metadata write-failure injection was not separately run; residual risk remains for block-device write-failure injection coverage beyond the corrupted metadata rejection path.
