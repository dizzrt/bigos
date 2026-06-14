## 1. 语义盘点与错误映射

- [x] 1.1 盘点 fd/VFS、exFAT、`/rw`、page/buffer cache、syscall 和 libc 的现有文件系统错误返回路径
- [x] 1.2 建立统一 VFS/backend 到稳定负 errno 的映射表，覆盖缺失路径、已存在目标、非法 fd、权限拒绝、只读拒写、容量耗尽、目录枚举缓冲不足、非法用户缓冲、不可 seek/enumerate 对象和 IO 失败
- [x] 1.3 对齐 syscall/libc/shell 可观察错误，确保用户态不依赖内部枚举名或调试字符串
- [x] 1.4 审查不可阻塞上下文守卫，确认路径解析、目录项变更、缓存装入/落盘和 metadata 查询不会在 IRQ、调度临界区或 preemption-disabled 路径执行阻塞副作用

## 2. fd/VFS 与后端分派硬化

- [x] 2.1 硬化共享路径解析后的后端分派，使 absolute 与 cwd-relative 操作解析到同一目标时具有一致行为
- [x] 2.2 确保只读 exFAT 对写入、创建、截断、删除和 rename 确定性拒绝，并且不修改 raw image、boot assets、mount state 或缓存状态
- [x] 2.3 确保跨后端 rename 和目录变更确定性失败，不修改任一后端、fd table、cwd 或 open file 引用
- [x] 2.4 审查 open file 引用、dup/dup2、exec 继承、退出/reap 与 offset 共享/独立规则，补齐与 unlink/rename 组合相关的不变量

## 3. `/rw` 可写后端一致性

- [x] 3.1 在 create/open-for-write/truncate/write/mkdir/unlink/rename/fsync 提交前执行最小 owner/mode、目录权限、对象类型、容量和后端可写性检查，并保持完整目录 execute/search bit 语义为非目标
- [x] 3.2 硬化失败路径，确保权限拒绝、容量耗尽、非法路径、非法用户缓冲和 IO 失败不会发布半成品 inode、目录项、dirty block、metadata 或 fd offset
- [x] 3.3 确保成功的 create/write/truncate/fsync 对同一 fd、dup 后 fd、继承 fd 和重新 open 后的 read/stat 行为一致可见
- [x] 3.4 确保成功的 mkdir/unlink/restricted regular-file rename 对路径查找、metadata 查询和最小目录枚举一致可见
- [x] 3.5 保持 `/rw` 为 RAM-backed 当前运行期一致性，不引入跨重启持久化、journaling、磁盘分区承载或 exFAT 写入
- [x] 3.6 确保 restricted regular-file rename 在目标已存在且非 no-op 时返回 `-EEXIST`，不执行目标替换或 POSIX atomic replacement

## 4. Metadata 与目录语义

- [x] 4.1 对齐 path metadata 与 fd metadata，使 type、size、mode、uid、gid 和 bounded defaults 反映成功运行期文件状态
- [x] 4.2 区分 unlink/rename 后的路径可见性与已打开 fd 引用，确保路径查询和 fd 查询按各自语义返回
- [x] 4.3 确保 metadata 查询失败不推进 offset、不修改 cwd/fd table/目录项/cache dirty state，并且不向用户缓冲发布 partial 或 uninitialized 数据
- [x] 4.4 确认 metadata contract 不暴露稳定 inode 身份、完整时间戳、ACL、xattr、设备节点、symlink 或跨重启持久化承诺

## 5. 用户态与行为验证

- [x] 5.1 增加或扩展行为验证，覆盖只读 exFAT 成功读/metadata、只读拒写、`/rw` create/write/read/stat/list、permission denied、missing path、invalid fd、非法对象类型、目录枚举缓冲不足返回 `-ERANGE`、rename 目标存在返回 `-EEXIST`
- [x] 5.2 增加用户态简单程序或 shell 可观察路径，验证 libc errno、shell 错误报告和文件状态在失败后仍可用
- [x] 5.3 覆盖 open-unlink、open-rename、dup offset、independent open offset、fork/exec fd 继承与 process reap close-all 的组合场景
- [x] 5.4 若涉及 Python helper 或脚本验证，使用 `uv run ...` 执行；若 `uv` 不可用，记录 blocker 而非改用系统 Python

## 6. 构建与运行验证

- [x] 6.1 运行 `xmake` 或当前环境可用的最窄 GCC cross-toolchain 构建，记录 `x86_64-elf-gcc`/`x86_64-elf-g++` 或 xmake 不可用时的 blocker 与残余风险
- [x] 6.2 对修改过的 C++ 源和头文件执行 clang 辅助检查，尽量匹配 freestanding C++17、x86_64 target、no exceptions、no RTTI 和项目 include paths；记录历史诊断、当前变更诊断和 false positive
- [x] 6.3 对修改过的 C++ 源和头文件执行 clangd/IDE diagnostics 辅助检查，修复当前变更新增的有效错误或记录 freestanding 配置缺口
- [x] 6.4 运行最相关的 QEMU headless smoke 或行为 marker 验证；若 QEMU、Bochs、cross-binutils、ROM/display、raw image、serial oracle 或 timeout 依赖不可用，明确记录跳过原因和残余 bootability 风险
- [x] 6.5 更新 validation notes，分离已通过检查、未运行检查及原因、历史问题、当前变更新增问题和剩余风险

## 7. 文档与边界同步

- [x] 7.1 更新相关 OpenSpec/archive 后续说明或 dedicated docs，保持 `/rw` 运行期一致性、只读 exFAT 差异和非目标边界一致
- [x] 7.2 如修改 `docs/en` 或 `docs/zh`，同步对应语言镜像并保持目录结构一致
- [x] 7.3 检查 `roadmap.md` 仍保持项目规划层级，不加入具体入口点、文件路径、命令、marker、源码实现细节或 archive 索引
