## 1. 块缓冲缓存（buffer cache）

- [x] 1.1 新增块缓冲缓存头与实现（落在 `kernel/core/fs/bcache` 一层）：定义 `BufferBlock`（dev、block_no、data、valid、dirty、ref_count、LRU 链接）与固定块大小（扇区大小的固定倍数），缓存数据页用 `alloc_kernel_pages`，缓存条目数为有界编译期常量。
- [x] 1.2 实现 `get(dev, block_no)`（命中返回；未命中分配并经 `block::read_sectors` 装入并标 valid）、`put(block)`（释放引用）、`mark_dirty(block)`、`sync(block)`/`sync_all()`（脏块经块写路径回写并清 dirty），全部 O(1)/有界、无递归落盘。
- [x] 1.3 实现确定性淘汰：优先淘汰 `ref_count==0` 的干净块；唯一可复用为脏块时先 `sync` 回写再复用；无可淘汰块返回 `nullptr` -> 上层 `-ENOMEM`/`-ENOSPC`，不死等阻塞、不在 IRQ 上下文落盘。
- [x] 1.4 上下文边界：装入/落盘只在可阻塞进程上下文进行；提供/记录由 syscall 层在分配/进入同步块 IO 前检查调度阻塞守卫的约定。
- [x] 1.5 内存/初始化/失败审查：缓存初始化顺序在块设备与内存就绪之后、装入失败与写回失败保持块状态一致（脏块不丢数据）、无 use-after-free、引用计数精确。

## 2. 块设备写路径（ATA PIO write）

- [x] 2.1 在 [block_device.h](include/drivers/block/block_device.h) 的 `BlockDevice` 追加 `WriteSectorsFn write_impl`（追加字段，不重排既有布局；`nullptr` 表示只读设备）并新增 `block::write_sectors(dev, lba, count, src, src_len)`，发起设备写前校验扇区数/源缓冲长度/LBA 溢出。
- [x] 2.2 在 ATA PIO 驱动实现 LBA28/LBA48 write-sectors + flush cache，复用现有 BSY/DRDY/DRQ 轮询时序；超时/设备错误返回 `DeviceTimeout`/`DeviceError`，由上层映射 `-EIO`。
- [x] 2.3 IRQ/端口 IO/硬件状态审查：写不破坏现有只读读路径与扇区大小契约、写后读时序正确、不在 IRQ-handler 上下文调用；只读设备（`write_impl==nullptr`）写返回 Unsupported -> `-EROFS`。

## 3. 可写文件系统后端（bigfs）

- [x] 3.1 实现最小可写 FS 后端布局（超级块、inode 位图、数据块位图、inode 表、定长目录项数组），固定块大小、有界 inode 数/文件大小/目录项数，全部经块缓冲缓存读写；提供 RAM-backed BlockDevice 作为默认验证介质（不改动现有磁盘镜像/MBR/exFAT 布局）。
- [x] 3.2 实现 inode 携带 owner（uid/gid）与 mode 元数据，挂载在与只读 exFAT 不同的挂载点，并确认只读 exFAT 的发现/挂载/读路径零改动。
- [x] 3.3 实现可写 open（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）、文件 `write`、`lseek`、`O_TRUNC` 截断（释放多余数据块）、`mkdir`、`unlink`；确定性失败语义（`-ENOSPC`/`-EEXIST`/`-ENOENT`/`-EISDIR`/`-EINVAL`/`-EIO`），失败路径不发布半成品元数据。
- [x] 3.4 实现写回一致性：write 经缓存即对读可见、落盘点为 `fsync`/淘汰回写/全量同步；元数据更新在提交前校验空间与合法性。
- [x] 3.5 内存/初始化/对象生命周期审查：位图分配/回收正确、inode/目录项引用一致、`unlink` 在无引用时释放、初始化顺序在块缓冲缓存就绪之后；无 journaling 的崩溃一致性限制在文档与验证记录中明确。

## 4. 管道与 fd 复制（pipe / dup / dup2）

- [x] 4.1 实现管道核心（`kernel/core/ipc` 或 `kernel/core/fs`）：有界环形缓冲、读/写端引用计数、读 `read_wq` 与写 `write_wq` 等待队列；读端/写端各自的 `FileOperations`（读端 read、写端 write，`lseek` 返回 `-ESPIPE`）。
- [x] 4.2 实现阻塞读写：读空且写端开 -> `read_wq` 阻塞、写入后唤醒；写满且读端开 -> `write_wq` 阻塞、读出后唤醒；阻塞只在可阻塞进程上下文，不可阻塞上下文确定性失败。
- [x] 4.3 实现 EOF/EPIPE：写端全关 -> 读返回 0；读端全关 -> 写返回 `-EPIPE`（`SIGPIPE` 投递为可选增强，依赖信号能力可用时）；端引用计数归零时确定性唤醒对端并回收管道对象。
- [x] 4.4 实现 `dup`/`dup2`：新 fd 指向同一 `File`、共享 offset、增 `File.ref_count`；`dup2` 先关闭已打开的 newfd 一次；close 减引用、归零释放；非法 fd 返回 `-EBADF`、无可用 fd 返回 `-EMFILE`。
- [x] 4.5 fork/exec/退出接线：fork 继承管道端 fd 并增端引用计数、exec 按 close-on-exec 关闭或保留、exit/reap 关闭所有管道端 fd 各一次；审查引用计数确保每个 fd 精确一次释放、无唤醒丢失/重复回收。

## 5. fd/VFS 壳层扩展

- [x] 5.1 在 [vfs.h](include/bigos/fs/vfs.h) 的 `FileOperations` 追加 `write` 与 `lseek` op、`File` 追加 `writable` 标志（追加字段，不破坏既有 `read`/`close` 与 `File` 布局），扩展 `open_absolute` 接受可写/创建 flags 与 `O_CREAT` 的 mode/owner 入参。
- [x] 5.2 在 [vfs.cc](kernel/core/fs/vfs.cc) 接线 `bigfs` 写后端与管道端 `File`；只读 exFAT 后端的 `write` op 返回 `-EROFS`；新增 `vfs::Status` 取值（`ReadOnlyFs`/`NoSpace`/`AccessDenied`/`NotSeekable` 等）映射到 [errno.h](include/bigos/errno.h)。
- [x] 5.3 确认现有只读 open/read/close 与只读 exFAT 行为零改动；fd 层支持管道端 `File` 与 `dup`/`dup2` 引用计数共享、exec 继承/close-on-exec。

## 6. syscall 接线

- [x] 6.1 在 [syscall.h](include/bigos/syscall.h) 的 `SyscallNumber` 末尾追加 `SYS_LSEEK = 20`、`SYS_PIPE = 21`、`SYS_DUP = 22`、`SYS_DUP2 = 23`、`SYS_FSYNC = 24`、`SYS_MKDIR = 25`、`SYS_UNLINK = 26`（最终集合以 spec 为准），不改动既有号位与寄存器 ABI 注释。
- [x] 6.2 在 [syscall.cc](kernel/core/syscall/syscall.cc) 的 `dispatch` 增加分支：`SYS_PIPE`/`SYS_DUP`/`SYS_DUP2`/`SYS_LSEEK`/`SYS_FSYNC`/`SYS_MKDIR`/`SYS_UNLINK`；扩展 `SYS_OPEN` 接受可写/创建 flags 与 mode、`SYS_WRITE` 支持写入文件/管道 fd；涉及分配/同步块 IO/阻塞的分支先检查调度阻塞守卫，全部不发 EOI。
- [x] 6.3 在 [errno.h](include/bigos/errno.h) 补齐缺失错误码（`EROFS`/`ENOSPC`/`EACCES`/`EPIPE`/`ESPIPE`/`EEXIST`/`EISDIR`/`EMFILE`/`EFAULT` 等），保持单一来源、不重复定义。
- [x] 6.4 中断/ABI 审查：确认新增/扩展分支不发送 i8259 EOI、不放宽异常/IRQ 门 DPL、不改变 rax 返回约定与向量布局；扩展 `SYS_OPEN`/`SYS_WRITE` 仅扩展语义不改号位。

## 7. 权限强制点接线

- [x] 7.1 在可写 FS 的 open（写/创建）、`write`、`mkdir`、`unlink` 执行前接入 [cred.cc](kernel/core/proc/cred.cc) 的 `may_access(...)` 作为实际强制点；确认其判定逻辑（root 放行、owner/group/other 匹配、非法输入拒绝）零改动。
- [x] 7.2 审查强制点：拒绝时不修改文件系统状态、返回确定性 `-EACCES`；只读后端写请求返回 `-EROFS`（按 spec 文档化顺序）；`O_CREAT` 新文件 owner 取调用进程身份、mode 取调用方传入。

## 8. 验证开关与 smoke

- [x] 8.1 在 `xmake.lua` 新增默认关闭开关 `writable_fs_smoke`（`BIGOS_WRITABLE_FS_SMOKE`）与 `pipe_smoke`（`BIGOS_PIPE_SMOKE`），保留现有 smoke 矩阵不删除。
- [x] 8.2 实现默认关闭的可写 FS smoke：发射 `BIGOS_WRITABLE_FS_PASSED`/`_FAILED`，覆盖「`O_CREAT` 建文件 + 写 + 读回一致」「`fsync` 落盘后强制淘汰再读一致」「owner/mode 权限拒绝」「只读后端写被 `EROFS` 拒绝」。
- [x] 8.3 实现默认关闭的 pipe smoke：发射 `BIGOS_PIPE_PASSED`/`_FAILED`，覆盖「跨进程写读 FIFO 一致」「读空阻塞 + 写入唤醒」「写端全关读 EOF」「读端全关写 EPIPE」「`dup`/`dup2` 共享 offset」。

## 9. C++ 辅助静态检查

- [x] 9.1 对新增/修改的 C++ 源与头运行 clang 与 clangd 辅助静态检查，尽量贴近 GCC 交叉构建环境（freestanding C++17、x86_64 目标、项目 include 路径、无 hosted 运行时/异常/RTTI）；若等价 flag 不可用则记录差距与残留风险。
- [x] 9.2 修复本次变更引入的 clang/clangd 错误，确认或修复有效新增告警；验证记录区分历史诊断、本次变更诊断与工具链/freestanding 误报。

## 10. 构建与运行时验证

- [x] 10.1 运行最窄可用构建（`xmake`）确认 buffer cache、`bigfs`、管道、块写路径、VFS 写/lseek op、syscall 分支编译通过；clang/clangd 仅作辅助信号，不替代 x86_64-elf-gcc 构建。
- [x] 10.2 在可用时运行 QEMU headless serial-marker smoke（`uv run python tools/boot_debug.py run --emulator qemu --display none ...` 并配 `--writable_fs_smoke=y` / `--pipe_smoke=y`），断言 `BIGOS_WRITABLE_FS_PASSED` 与 `BIGOS_PIPE_PASSED`；涉及块设备写与缓存一致性时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证。
- [x] 10.3 若 QEMU/Bochs、ROM/显示、交叉工具链或磁盘镜像不可用，显式记录缺失工具、跳过的验证、替代检查与残留风险，不得声称已做运行时验证。

## 11. 源码契约/行为断言测试

- [x] 11.1 用 `uv run pytest` 增补源码契约/行为断言测试（沿用behavior assertion validation baseline 启动的行为断言轨道）：覆盖新增 syscall 号位固定（`SYS_LSEEK=20`..`SYS_UNLINK=26`）、`SYS_OPEN`/`SYS_WRITE` 扩展语义、`BlockDevice` 写入口与 `FileOperations` 写/lseek op 存在、buffer cache 写回/淘汰路径、`may_access` 接线于可写路径、管道 EOF/EPIPE 与 dup 共享 offset，以及 smoke marker 行为断言。
- [x] 11.2 对新增/修改的 Python 文件运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`，修复新引入的 lint/类型/格式/测试问题；若 `uv` 不可用则显式记录该阻塞。

## 12. 文档与验证记录

- [x] 12.1 更新相关文档（`docs/en` 为 canonical，`docs/zh` 同步匹配相对路径）：记录块缓冲缓存语义、可写 FS（`bigfs`）布局与挂载、写回一致性与 `fsync`、块写路径、管道/`dup` 语义、新增/扩展 syscall 号、owner/mode 强制点；不暗示更改 boot/向量/DPL/页表/CR3/ABI/磁盘镜像/MBR/exFAT 只读布局。
- [x] 12.2 整理验证记录：分别列出已通过检查、因依赖缺失无法运行的检查与原因及残留风险、历史诊断、本次变更引入的问题；明确非目标（硬/软链接、`rename`、file-backed mmap、`readdir` 完整遍历、多挂载、journaling、ACL/xattr、FIFO/socket、SMP 缓存一致性、写性能优化）与已知限制（无 journaling 的崩溃一致性、`SIGPIPE` 为可选）。
