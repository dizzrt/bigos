## 1. 缓存回写基础

- [x] 1.1 开发：梳理 `include/bigos/fs/bcache.h` 与 `kernel/core/fs/bcache.cc` 的现有同步接口，明确 device-scoped/selective write-back API 形态，并保持 freestanding-safe、无动态依赖、无 async I/O。
- [x] 1.2 开发：实现或整理 `sync_device()` 或等价设备范围 dirty block 同步路径，确保只清除成功写回的 dirty block，首个失败以确定性状态返回，失败块保持 dirty 或 pending；保留 `sync_all()` 作为调试/全局内部工具。
- [x] 1.3 开发：复核 dirty victim 淘汰路径，使写回失败时 slot 不复用、原 device/block key 不丢失、调用方得到确定性失败。
- [x] 1.4 输出：更新缓存头文件注释和实现内局部注释，说明 device-scoped/selective sync、dirty 保留和 blockable context 边界。
- [x] 1.5 回归：补充源码级检查覆盖 `sync_device()`/device-scoped sync、`sync_all()` 不作为持久成功路径、dirty victim 失败保留、无关设备不扩大同步承诺。

## 2. bigfs 同步路径收束

- [x] 2.1 开发：将 persistent `/rw` 的 `fsync`、metadata commit flush 和验证用 cache invalidation/eviction 路径收束到 page/buffer cache write-back API。
- [x] 2.2 开发：保证 pending metadata commit 在 clean-sync 成功前完成，失败时保留 pending/dirty 状态并返回 deterministic error。
- [x] 2.3 开发：复核文件创建、目录创建、unlink/rmdir、rename、file growth、truncate 的 metadata commit plan，确保数据块、inode、目录项、bitmap、volume metadata 的同步顺序符合 ordered-write 契约。
- [x] 2.4 输出：更新 bigfs 源码级说明或 validation notes，明确该能力只提供 clean-sync，不声明 journaling、crash recovery 或 power-loss recovery。
- [x] 2.5 回归：补充源码级检查覆盖 `fsync` 经 commit plan 和 cache sync、提交失败不 reset 成功、淘汰不绕过 ordered metadata commit。

## 3. 用户态显式同步

- [x] 3.1 开发：新增有界显式同步 syscall/VFS 入口，将请求路由到当前 writable backend 的 metadata commit 和 device-scoped cache sync，不改变既有 syscall 语义、启动 ABI、IDT/syscall vector 或用户态结构体 ABI。
- [x] 3.2 开发：在用户态 libc 中新增 `sync()` wrapper，按现有 `int 0x80` ABI 调用内核，成功返回 `0`，失败设置 `errno` 并返回 `-1`。
- [x] 3.3 开发：在 `/bin/sh` 中新增 `sync` 内建命令，直接在 shell 进程调用 libc `sync()`，并通过现有 stdout/stderr 路径报告确定性错误。
- [x] 3.4 输出：更新相关头文件、用户态说明或 validation notes，明确这是 bounded writable-backend sync，不声明完整 POSIX `sync(2)`、`fdatasync`、async write-back 或 crash recovery。
- [x] 3.5 回归：补充源码级检查覆盖 syscall number/dispatch、libc wrapper、shell builtin、errno 翻译和非完整 POSIX 边界。

## 4. 不可阻塞上下文边界

- [x] 4.1 开发：复用现有 scheduler/preemption/IRQ 状态查询为 cache/VFS sync path 增加轻量 guard；若某类状态没有稳定查询接口，记录为调用点约束和文档化诊断，不新增通用上下文跟踪机制。
- [x] 4.2 回归：补充源码级检查覆盖显式同步、`fsync`、cache eviction 和 metadata commit 不从 IRQ、scheduler critical section 或 preemption-disabled 路径发起阻塞 IO。

## 5. 运行时验证

- [x] 5.1 开发：扩展默认关闭 persistent writable smoke，使 write-run 覆盖 `fsync` 后 clean reboot readback、shell/libc `sync` 后 clean reboot readback、dirty eviction 后 reload readback、metadata/freespace 同步后的验证。
- [x] 5.2 开发：为回写失败或环境不可用路径保留明确 marker、skipped/blocked 记录或 validation note，不把未执行的 emulator 验证报告为 passed。
- [x] 5.3 输出：如涉及文档，保持 `docs/en` 与 `docs/zh` 对应路径同步，并使用 repository-relative path，不在 roadmap 中加入实现入口、命令或 marker 细节。
- [x] 5.4 回归：在工具链可用时运行 persistent writable 相关 QEMU headless write-run/verify-run smoke；若 xmake、`x86_64-elf-*`、QEMU/Bochs、ROM/display、serial capture 或持久测试盘不可用，记录 blocker 与残余风险。

## 6. 静态检查与构建

- [x] 6.1 回归：运行 narrow source-level pytest，例如 `uv run pytest tests/test_writable_fs_page_cache_pipe_source.py tests/test_metadata_consistency_source.py tests/test_stable_file_growth_source.py tests/test_fd_vfs_shell_source.py tests/test_user_c_baseline_source.py`，并修复本 change 引入的失败。
- [x] 6.2 回归：若修改或新增 Python 测试/脚本，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`；若 `uv` 不可用，记录 blocker。
- [x] 6.3 回归：运行 xmake 交叉构建或等价窄构建，确认 C++/C/assembly 变更可由 `x86_64-elf-*` toolchain 编译；工具链不可用时记录缺失项和残余风险。
- [x] 6.4 回归：对修改过的 C++ 源和头文件执行 clang/clangd 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI 配置；区分历史诊断、当前变更诊断和 freestanding 配置误报。

## 7. 收尾

- [x] 7.1 输出：运行 `openspec status --change add-buffer-cache-writeback`，确认 proposal、design、specs、tasks 均完成且 apply-ready。
- [x] 7.2 输出：整理验证记录，分别列出已通过检查、因环境不可用跳过/阻塞的检查、当前变更引入并已修复的问题、剩余风险。
