## 1. 契约盘点与边界确认

- [x] 1.1 盘点 `kernel/core/fs`、`kernel/core/syscall`、`kernel/core/proc`、`user/libc`、`user/bin` 和 `user/sh` 中现有 runtime filesystem maturity 相关路径，确认哪些行为已满足规格、哪些需要修复。
- [x] 1.2 确认 `/rw` 仍为 RAM-backed current-session backend，记录不改动 MBR、exFAT boot assets、磁盘镜像布局、boot 地址、linker 地址和 syscall vector 的实现约束。
- [x] 1.3 为 read-only exFAT 与 `/rw` 的后端差异列出用户态可观察矩阵，覆盖 open/read/write/lseek/fsync/stat/fstat/readdir/mkdir/unlink/rename。

## 2. fd/VFS 组合语义

- [x] 2.1 统一 path-taking 操作的 cwd-relative 解析入口，确保 open、stat、mkdir、unlink、rename 和目录枚举 setup 对等价相对/绝对路径行为一致。
- [x] 2.2 审查并修复 fd/VFS 后端分派，确保 read-only exFAT 写/创建/删除/rename 请求稳定拒绝且不修改状态。
- [x] 2.3 审查并修复 fd table 与 open file object 引用生命周期，确保 dup/fork/exec-inherited fd、unlink 后 fd、rename 后 fd 都按规格保持有效或释放。
- [x] 2.4 稳定 fd/VFS errno 映射，覆盖 missing path、existing target、invalid fd、invalid buffer、read-only write、permission denial、capacity exhaustion、unsupported object 和 backend I/O failure。

## 3. `/rw` 运行期一致性

- [x] 3.1 审查并修复 `/rw` create/write/truncate/read/lseek/fsync 的提交顺序，确保成功写入对后续 read 和 metadata 可见，失败写入不推进 offset。
- [x] 3.2 审查并修复 `/rw` mkdir/unlink/restricted regular-file rename 的目录项更新顺序，确保成功目录变更对 lookup、metadata 和按目录 slot 稳定顺序的最小目录枚举可见。
- [x] 3.3 审查并修复 owner/mode、父目录可写性、对象类型、路径长度、文件大小、inode、data block、directory slot 和 cache block 容量检查，确保失败不发布半成品状态，并支持通过自然填满 `/rw` 触发真实容量边界。
- [x] 3.4 审查并修复 `fsync` 与 cache eviction 行为，确保当前 RAM-backed backend 在同一 boot session 内同步后淘汰再读一致，并明确不承诺 reboot persistence。

## 4. metadata、目录枚举与用户态观察

- [x] 4.1 审查并修复 path metadata 与 fd metadata，确保 create/write/truncate/mkdir/unlink/rename 后 type、size、mode、uid、gid 和 bounded defaults 一致。
- [x] 4.2 实现或修复 unlink/rename 后 path metadata 与 fd metadata 的区分：旧路径按目录项可见性失败，仍打开 fd 在关闭前继续可查询。
- [x] 4.3 审查并修复最小目录枚举，确保目录 fd 按稳定后端顺序返回有界 name/type 记录：`/rw` 使用目录 slot 顺序，exFAT 使用后端遍历顺序；非目录、pipe、非法 fd、缓冲不足和非法用户缓冲稳定失败。
- [x] 4.4 审查并修复 libc wrapper、shell 报错和小型用户工具输出，确保用户态通过 `errno` 观察确定性成功/失败结果。

## 5. 验证与文档

- [x] 5.1 增加或扩展 source-level checks，覆盖 fd/VFS dispatch、open file reference、metadata、目录枚举、权限、容量和失败状态保持不变量。
- [x] 5.2 新增专用默认关闭 filesystem maturity runtime smoke 或配套小型静态 C 验证程序，覆盖 read-only exFAT 成功路径和 `/rw` create/write/read/lseek/fsync/stat/list/unlink/restricted-rename 成功组合。
- [x] 5.3 增加失败路径验证，覆盖 read-only write、missing path、existing target、invalid fd、invalid user buffer、permission denial、通过自然填满 `/rw` 触发的真实 capacity edge、unsupported object 和 enumeration output exhaustion。
- [x] 5.4 更新相关文档或验证记录，明确 `/rw` 只保证 current-runtime consistency，不声明 cross-reboot persistence、journaling、full POSIX FS、full `DIR*` 或 stable inode identity。

## 6. 构建与运行验证

- [x] 6.1 运行窄范围 xmake 构建，记录 x86_64-elf toolchain 是否可用；若不可用，记录 blocker、替代检查和剩余风险。
- [x] 6.2 对修改过的 C++ 源和头文件运行可行的 clang/clangd 辅助静态检查，使用 freestanding C++17、x86_64 target、无 exceptions、无 RTTI 的近似配置；若工具或配置不可用，记录原因和风险。
- [x] 6.3 在可用环境中运行相关默认关闭 filesystem/userland runtime smoke；优先使用 QEMU headless 和串口 marker，若 QEMU、Bochs、ROM/display、raw image、serial oracle 或 timeout 依赖不可用，记录 skipped。
- [x] 6.4 汇总 validation notes，区分已通过检查、跳过检查、历史诊断、当前 change 引入的问题和剩余 bootability/runtime 风险。
