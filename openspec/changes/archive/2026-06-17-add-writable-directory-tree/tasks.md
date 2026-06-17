## 1. 开发

- [x] 1.1 审查 `kernel/core/fs/bigfs.cc`、`kernel/core/fs/vfs.cc`、`include/bigos/fs/bigfs.h`、`include/bigos/fs/vfs.h`、`kernel/core/syscall/syscall.cc` 和 `kernel/core/proc/proc.cc` 的现有目录、fd、cwd、metadata、readdir、unlink 与 rename 边界，记录目录树实现缺口。
- [x] 1.2 在 bigfs 内补齐多层目录树路径解析辅助逻辑，覆盖父目录查找、中间组件类型检查、名称长度、目录项容量、inode/data block 容量和确定性错误映射。
- [x] 1.3 实现目录内多文件创建/删除的一致性修正，确保失败路径不发布半成品 inode、目录项、fd、dirty cache 状态或错误 offset。
- [x] 1.4 实现空目录删除核心语义，覆盖空目录成功删除、非空目录拒绝、常规文件错误类型、只读后端拒绝、容量/IO/权限失败和目录引用可解释性。
- [x] 1.5 追加独立 `SYS_RMDIR`、内核分发、VFS/bigfs 入口、libc wrapper、头文件声明和用户态路径工具；只允许追加 syscall 编号，不得重排既有编号或改变 `int 0x80` 寄存器 ABI。
- [x] 1.6 为 cwd/path state 引入 deleted-directory cwd 语义，确保被 `SYS_RMDIR` 删除的目录项不再被新 lookup 找到，但已有 cwd/open-directory 引用在释放前保持确定性 `getcwd`、相对 lookup、`chdir("..")` 和延迟释放行为。
- [x] 1.7 更新 shell/path tools 组合行为，使 mkdir、rmdir、rm、ls、stat、相对路径和错误输出能表达目录树成功、失败与 deleted-directory cwd 状态。
- [x] 1.8 保持 RAM-backed 与 persistent clean-sync `/rw` 使用同一目录树语义，并确保 persistent 后端只扩大 clean-sync 可见性，不声明 crash recovery。
- [x] 1.9 检查所有新增目录树路径是否只在可阻塞进程上下文执行分配、block cache 装入、同步块 IO 和 dirty cache 写回；不可阻塞上下文必须确定性失败或进入已文档化诊断路径。
- [x] 1.10 检查 C++ freestanding 约束、头文件最小包含、显式内存分配 API、对象生命周期、引用计数、对齐、容量耗尽和回滚路径，避免引入 hosted libc、异常、RTTI、线程或 OS 服务假设。
- [x] 1.11 审查 boot/layout/ABI 影响，确认未改变 Legacy BIOS/MBR/exFAT boot image、UEFI boot spike、boot handoff ABI、linker 地址、页表布局、中断向量、既有 syscall 编号/寄存器 ABI 或磁盘分区布局。

## 2. 输出

- [x] 2.1 扩展默认关闭的 writable/runtime filesystem smoke，覆盖嵌套目录创建、多文件创建/删除、`SYS_RMDIR` 空目录删除、非空目录删除拒绝、只读后端拒绝、权限拒绝、容量耗尽回滚、目录枚举一致性、deleted-directory cwd 和已打开文件 unlink 引用语义。
- [x] 2.2 扩展 userland smoke 或 packaged path-tool transcript，覆盖 shell 中 `cd /rw` 后的相对路径目录树操作、`rmdir`/`ls`/`stat` 可观察性、错误 errno 输出、deleted-directory cwd 行为和 shell 存活性。
- [x] 2.3 扩展已有 persistent writable smoke 的双阶段 marker，在写入阶段创建/同步目录树，在验证阶段用同一 persistent test disk 确认 clean reboot 后可见，并覆盖未同步或同步失败不扩大持久性承诺。
- [x] 2.4 如新增用户态 wrapper 或工具，更新 `user/libc/include/**`、`user/libc/**`、`user/bin/**`、构建规则和必要的最小 man/help 文本，保持 bounded/non-POSIX 表述。
- [x] 2.5 如更新文档，先更新 `docs/en` canonical，再同步 `docs/zh` 对应路径；文档必须使用仓库相对路径，不写入本地绝对路径、命令输出索引或 roadmap 任务编号。
- [x] 2.6 记录实现期验证结果，分开列出已通过检查、无法运行的检查及原因、历史诊断、当前变更引入的问题、工具链/模拟器/ROM/display/磁盘镜像缺口和残余风险。

## 3. 回归

- [x] 3.1 运行 `openspec validate add-writable-directory-tree --strict`，修复当前 change artifacts 的格式、requirement/scenario 或 delta 问题。
- [x] 3.2 运行 `xmake`，确认 x86_64-elf GCC/G++ 交叉构建通过；如 `x86_64-elf-*` 或 xmake 不可用，记录 blocker 和残余风险。
- [x] 3.3 针对改动的 C++ 源码和头文件运行 clang 辅助静态检查，配置为 freestanding C++17、x86_64 target、项目 include paths、禁用 exceptions/RTTI；修复当前变更引入的有效诊断，记录历史诊断或配置 false positive。
- [x] 3.4 针对改动的 C++ 源码和头文件运行 clangd 辅助检查或等价索引诊断；如 clangd 无法获得等价 cross-toolchain 参数，记录差距和残余风险。
- [x] 3.5 运行目录树相关默认关闭 smoke 的 QEMU headless 串口 marker 验证，优先使用 `uv run python tools/boot_debug.py run --emulator qemu --display none ...`；如 uv、QEMU、ROM/display、cross-toolchain 或磁盘镜像不可用，记录为 skipped/blocked。
- [x] 3.6 对 persistent clean-sync 目录树验证运行已有 persistent writable smoke 的双阶段 marker 流程，确认同步后的目录树状态 clean reboot 后可见；如环境不可用，记录缺口且不得声称 runtime-passed。
- [x] 3.7 在可用时用 Bochs 或 Bochs/QEMU 交叉验证目录树变更未破坏 Legacy BIOS/ATA PIO/block cache 行为；如 Bochs 环境不可用，记录跳过原因。
- [x] 3.8 运行定向搜索，确认新增文档和 OpenSpec artifacts 没有引入 `Stage N`、`阶段 N` 或把 roadmap 任务编号写作 change/spec 标识。
