## Context

当前 BigOS 的文件访问路径仍是内核内部直连：`fs_smoke` 和 `user_elf_smoke` 通过 ATA PIO、MBR exFAT discovery、`mount_exfat`、`lookup`、`read_file` 直接读取文件。进程生命周期已经具备 PID、父子关系、`wait`/`exit`、bounded ELF64 `exec argv/envp` 和 safe reaper，但进程对象尚未拥有 fd table，用户态也没有稳定的 `open`/`read`/`close` I/O 边界。

阶段 13 的目标是在不引入 writable filesystem、page cache、VMA/demand paging 或 libc 的前提下，为后续 userland runtime 提供最小且可验证的 I/O 抽象。实现必须保持 x86_64 单核、Legacy BIOS/MBR/exFAT、同步 ATA PIO、read-only filesystem、`int 0x80` ABI、freestanding C++17/C17 和 blocking-context 约束。

## Goals / Non-Goals

**Goals:**

- 定义最小 VFS 壳层：root mount、vnode、open file、file operations 和 read-only exFAT backend adapter。
- 为每个 `Process` 增加有界 fd table，支持 fd 分配、lookup、引用释放、`exec` 继承/关闭和 exit/reap 回收。
- 增加 `SYS_OPEN`、`SYS_READ`、`SYS_CLOSE`，并明确参数 ABI、错误返回、用户 path/buffer copy 和阻塞上下文规则。
- 迁移至少一个现有 consumer，使 `fs_smoke` 或 `user_elf_smoke` 能通过 VFS/open/read/close 路径验证只读文件读取。
- 保持现有 exFAT on-disk、ATA PIO、syscall vector、页表地址布局和进程 teardown 安全边界不变。

**Non-Goals:**

- 不实现 write syscall 接入普通文件、文件写入、目录变更、权限、uid/gid、mount namespace、multiple filesystem backends 或 removable media。
- 不实现 page cache、buffer cache、async I/O、poll/select/epoll、pipe/socket、dup/fcntl、lseek、stat、cwd 或相对路径。
- 不实现 `fork` fd table 复制、COW、signals、VMA/demand paging、`mmap`/`brk`、user-space libc、dynamic linker、SMP 或 UEFI backend。
- 不改变现有 raw image/MBR/exFAT 磁盘布局、`int 0x80` 入口、GDT/TSS/RSP0、higher-half/direct-map/KVMEM/self-map 常量。

## Decisions

- 第一版 VFS 采用单 root mount 和只读 backend。`vfs::init()` 挂载当前 raw image 的 exFAT root，`vfs::open_absolute(path, flags)` 只接受绝对路径和 read-only flags，并把 exFAT `FileMetadata` 包装为 vnode/private backend state。替代方案是直接把 exFAT API 暴露给 syscall，但这会让 userland 与具体文件系统耦合，并阻塞后续 page cache/VFS 扩展。
- `File` 对象保存 file offset 和 backend operations，`Vnode` 保存只读 metadata。`read(fd, buf, len)` 从 `File.offset` 开始调用 backend read，成功后推进 offset；offset arithmetic 必须检查溢出并在 EOF 处返回 0。替代方案是让每次 read 都要求显式 offset，但这不符合常见 fd 语义，也会把 `pread` 需求提前。
- 每个 `Process` 持有固定容量 fd table。fd table entry 指向 ref-counted `File`，`open` 分配最低可用 fd，`close` 释放 entry 并 drop file ref；进程 exit/reap 关闭所有仍打开 fd。第一版不引入全局可增长 fd allocator 或复杂锁，因为当前调度边界仍是单核。
- `exec` 默认继承 fd，第一版仅保留 close-on-exec 的内部字段和 commit-time 处理，不对用户态暴露 `fcntl`、`O_CLOEXEC` 或独立 flag API。这样 future userland 可以跨 `exec` 保留已打开文件，同时为后续关闭敏感 fd 保留实现钩子。替代方案是立即暴露 close-on-exec 用户 API，但这会把 `fcntl`/open flags 设计提前到 fd/VFS 壳层阶段之外。
- `open` path 从用户态复制到 bounded kernel buffer 后再进入 VFS。path 必须是 NUL 结尾、绝对路径、长度受限且不包含未支持的相对解析；非法用户指针或过长路径返回确定性负错误码或终止当前进程。替代方案是在 VFS 中逐字节访问用户内存，但这会把用户地址空间校验扩散到 filesystem backend。
- syscall read/write 边界在阶段 13 保持非对称。`SYS_READ` 使用 fd table 查找 `File` 并复制到用户 buffer；`SYS_WRITE` 继续保留现有 `fd == 1` stdout/serial 特例，不把 console/stdout 建模为 fd table entry。本阶段普通文件写入返回确定性错误。替代方案是把 stdout 立即迁移为 console file，但这会引入 character device/VFS 节点策略，超出只读 regular-file fd/VFS 壳层目标。
- fd/VFS 操作只允许在可阻塞上下文执行。VFS open/read 可能触发 ATA PIO 和 `kmalloc`，因此 syscall dispatch 必须拒绝或诊断 IRQ、preemption-disabled、scheduler critical section 等不可阻塞上下文。替代方案是在所有 VFS 路径中改造成 nonblocking，但这会提前引入 async I/O 和复杂状态机。
- 验证以 source-level checks 加 QEMU headless serial marker 为主。第一版复用并迁移现有 `fs_smoke` 到 VFS open/read/close 路径，不新增独立 `fd_vfs_smoke` 开关；smoke 覆盖 successful open/read/close、not-found、bad-fd、EOF clamp 和 close 后 read，用户态 invalid-buffer 与 exec inheritance 由 source-level checks 或 user smoke 扩展覆盖。涉及 ATA PIO、port-IO 或 emulator 行为时在可用环境补充 Bochs 或 QEMU+Bochs 交叉验证。

## Risks / Trade-offs

- VFS 抽象过早泛化 -> 只实现单 root、绝对路径、只读 regular file、固定 operation table，不引入 mount namespace 或多 backend 策略。
- fd/file 生命周期泄漏 -> 通过固定容量 table、明确引用计数、exit/reap close-all、source-level refcount checks 和 double-close 场景控制。
- read offset 与 EOF/overflow 出错 -> 所有 offset+length arithmetic 使用 checked bounds，EOF 返回 0，超过文件大小只读可用字节。
- syscall 用户 buffer 破坏内核内存 -> 复用 `proc::validate_user_buffer`/copy helper，path/buffer 均先验证再拷贝，不让 filesystem backend 直接解引用用户指针。
- 阻塞上下文误用 -> 在 syscall/VFS 入口检查 blocking guard，文档化 IRQ 和 preemption-disabled 禁用规则，source-level checks 覆盖 forbidden contexts。
- exFAT backend 与 VFS 状态重复 -> 第一版以 adapter 包装 `ExfatMount` 和 `FileMetadata`，不重写 exFAT parser；后续需要 page cache 时再重构 vnode identity。
- emulator smoke 不稳定 -> validation 必须记录 QEMU/Bochs/cross-toolchain/ROM/serial oracle 可用性、跳过原因、替代检查和残余 bootability 风险。

## Migration Plan

- 第一步：新增 VFS/fd public headers 与实现文件，保持现有 exFAT API 不破坏，先让 kernel-only caller 可通过 VFS 打开和读取 `/boot/fs_smoke.txt`。
- 第二步：在 `proc::init()` 或 process 创建路径初始化 fd table，并在 process exit/reap 与 exec commit 边界接入 fd close/inherit 规则。
- 第三步：扩展 syscall number 和 dispatch，增加 `open`、`read`、`close`，复用用户 path/buffer copy 与 blocking-context guard。
- 第四步：迁移现有 `fs_smoke` 到 VFS open/read/close 路径，通过既有 `fs_smoke` 开关和 serial marker 验证 open/read/close、EOF、not-found、bad-fd 和 close 后 read。
- 第五步：更新 `docs/en` canonical 文档和 `docs/zh` 镜像，记录 fd/VFS syscall ABI、非目标和验证方法。
- 回滚策略：若 syscall 暴露不稳定，保留 kernel-only VFS adapter 和原 exFAT smoke 路径，临时禁用用户 fd syscall switch；不得删除或破坏现有 read-only exFAT 直连 API。

## Resolved Decisions

- 阶段 13 不把 `SYS_WRITE` 的 stdout 建模为 fd table entry；保留现有 `fd == 1` stdout/serial 特例，只为普通只读文件实现 `open`/`read`/`close` fd 路径。
- 阶段 13 不对用户态暴露 close-on-exec flag 或 `fcntl`；仅保留 fd table entry 的内部 close-on-exec 字段，并定义 `exec` commit 时的处理规则。
- 阶段 13 不新增 `fd_vfs_smoke` 开关；复用并迁移现有 `fs_smoke` 执行 VFS open/read/close 路径验证。
