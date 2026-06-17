## 1. 规格与边界确认

- [x] 1.1 审查 `writable-filesystem`、`fd-vfs-shell`、`syscall-entry`、`user-libc-min`、`posix-like-process-io-subset` 和 `runtime-smoke-validation` delta specs，确认运行时文件系统边界只包含最小目录枚举，不包含持久化、rename、link、完整目录遍历、完整 POSIX `readdir/getdents` 兼容、async I/O、SMP 或 broad file-backed `mmap`
- [x] 1.2 审查现有 `/rw`、只读 exFAT、page/buffer cache、fd/VFS、syscall、libc 和 shell 重定向实现，记录已满足项、缺口和需要保持不变的 ABI/boot/disk/layout 假设
- [x] 1.3 确认本 change 不修改 boot 地址、linker 地址、IDT/syscall vector、CR3 切换、page-table layout、MBR/exFAT 磁盘布局或现有 syscall 寄存器约定

## 2. 可写文件系统与缓存

- [x] 2.1 补齐或修正 `/rw` 文件创建、打开、读取、写入、seek、truncate、fsync、mkdir、最小目录枚举和 unlink 的有界行为，确保容量、路径、权限、只读后端和 IO 失败返回确定性错误
- [x] 2.2 实现或验证写后读一致性、`fsync` 写回、缓存淘汰后读回一致和块 IO 失败保留脏状态的运行期语义
- [x] 2.3 实现或验证 unlink 已打开文件时先移除目录项并使新的路径查找不可见，同时保留 open fd 对 inode 与数据块的访问直到最后一个 open fd 关闭
- [x] 2.4 审查可写后端元数据、inode、目录项、数据块、open file 引用和缓存块的生命周期，覆盖分配失败、回滚、引用归零、最后 fd 关闭后的整体资源释放和进程 reap 路径
- [x] 2.5 实现或验证最小目录枚举只返回有界目录项名称和基础类型，并对非目录、非法 fd、过小 buffer 和非法用户缓冲返回确定性错误

## 3. fd/VFS 与进程组合

- [x] 3.1 补齐或验证 fd/VFS 对只读 exFAT 与 `/rw` 后端的路由边界，确保只读写请求返回拒写错误且不改变只读路径语义
- [x] 3.2 验证 open file offset 语义：dup/dup2 共享 offset，独立 open 使用独立 offset，fork/exec 继承和 close-on-exec 行为符合规格
- [x] 3.3 验证进程退出、异常终止和 safe reaper 路径精确关闭剩余文件 fd，避免 use-after-free、重复释放或泄漏
- [x] 3.4 确保 fd/VFS 文件操作只在允许阻塞、分配和同步块 IO 的普通进程上下文执行，不可阻塞上下文确定性拒绝或诊断

## 4. syscall 与用户内存边界

- [x] 4.1 补齐或验证文件相关 syscall 的 append-only ABI 和既有 `SYS_OPEN`/`SYS_WRITE` 语义扩展，保持 syscall vector、号位、寄存器约定、DPL 和 no-EOI 规则不变
- [x] 4.2 补齐或验证 `open`/`mkdir`/`unlink` path 的有界 NUL 终止复制，以及 `read`/`write`/目录枚举 buffer 的 VMA-backed 读写权限校验和溢出检查
- [x] 4.3 补齐或验证 `lseek`、`fsync`、`mkdir`、最小目录枚举、`unlink`、非法 fd、不可 seek 对象、只读后端、权限拒绝和容量耗尽的负 errno 返回
- [x] 4.4 在进入可能分配、等待或同步块 IO 的 syscall 路径前检查调度阻塞守卫，失败时不发布部分 fd、目录项、inode 或缓存写入

## 5. 用户态 libc、程序与 shell

- [x] 5.1 补齐或验证用户态 libc 文件 I/O wrapper、最小目录枚举 wrapper、细粒度头文件、open flags、seek whence、mode/off_t/ssize_t 类型和 errno mirror，避免依赖宿主头
- [x] 5.2 确保 wrapper 成功时返回用户态语义值、失败时设置正 errno 并返回失败哨兵，成功调用不清零或改写 errno
- [x] 5.3 优先复用或扩展现有 userland smoke 作为简单用户态文件行为消费者，覆盖创建、写入、seek、读回、fsync、mkdir、最小目录枚举、unlink、只读拒写和典型错误报告；只有复用导致用例耦合过大时才拆出小型专用用户程序
- [x] 5.4 验证 shell 输出/输入重定向与文件 fd 组合稳定，重定向失败时 shell 报告错误并保留自身标准 fd 与无关 fd

## 6. 文档与 OpenSpec

- [x] 6.1 更新相关英文文档时同步更新 `docs/zh` 镜像，明确 `/rw` 仅保证运行期一致性且不承诺跨重启持久化
- [x] 6.2 保持 `roadmap.md` 仅记录规划级 TTY console input capability4 能力和边界，不写入具体入口、命令、marker、源码细节或归档索引
- [x] 6.3 运行 `openspec status --change "improve-runtime-filesystem-usability"` 和严格校验，修正 proposal/design/spec/tasks 的结构或格式问题

## 7. 构建、静态检查与运行时验证

- [x] 7.1 运行最窄有用的 `xmake` / cross-toolchain 构建；若 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake 或磁盘镜像不可用，记录缺失条件、替代检查和残余风险
- [x] 7.2 对修改的 C++ 源、头文件或构建配置运行 clang/clangd 辅助静态检查；使用 freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI 的近似配置，并区分历史诊断和本 change 新增诊断
- [x] 7.3 运行或扩展源码/行为断言，覆盖 fd/VFS offset、引用生命周期、unlink 打开文件、最小目录枚举、用户缓冲校验、errno 映射、`fsync` 写回和失败回滚
- [x] 7.4 环境具备时运行 QEMU headless 运行时验证，覆盖运行时文件系统用户程序和 shell 重定向路径；若 QEMU、Bochs、ROM/display、串口 oracle 或 disk image 不可用，记录跳过原因和剩余 bootability 风险
- [x] 7.5 如执行 Python 辅助脚本或 pytest，使用 `uv run ...`；若 `uv` 不可用，明确记录 blocker，且不静默改用系统 Python
