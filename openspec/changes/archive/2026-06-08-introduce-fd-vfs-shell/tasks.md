## 1. VFS 壳层和 exFAT backend

- [x] 1.1 新增最小 VFS/fd 头文件和源文件结构，定义 `Vnode`、`File`、file operations、root mount、错误码映射和只读 flags，并保持 public header include 最小化
- [x] 1.2 实现 `vfs::init()` 或等价初始化入口，通过现有 block/exFAT 路径挂载单 root 只读卷，失败时不发布部分初始化 mount
- [x] 1.3 实现 exFAT backend adapter，将 `ExfatMount`、`lookup`、`FileMetadata`、`read_file` 包装为 VFS open/read operations，不重写 exFAT parser 或改变 on-disk 支持范围
- [x] 1.4 实现绝对路径 read-only open，拒绝相对路径、过长路径、非 regular file、missing path、write/create/truncate flags 和 unsupported path forms
- [x] 1.5 实现 open file read offset 语义，覆盖 EOF 返回 0、成功读推进 offset、失败不推进 offset、offset/length 溢出检查和 backend read error 传播

## 2. 进程 fd table 生命周期

- [x] 2.1 在 `Process` 中增加有界 fd table、fd entry、open file reference 关系和 close-on-exec 内部标志，避免引入 SMP 锁或动态无限增长策略
- [x] 2.2 在 process 创建/发布路径初始化 fd table，并在初始化失败时 rollback 已分配 process/user/kernel 资源
- [x] 2.3 实现 fd allocate/lookup/close helpers，分配最低可用 fd，处理 table full、bad fd、double close 和 non-readable file 的确定性错误
- [x] 2.4 在 `exec` commit 边界实现 fd 继承和 close-on-exec 关闭规则，并确保 exec rollback 不破坏旧 fd table
- [x] 2.5 在 `exit`、fault termination、zombie/reap 和 safe reaper 路径关闭所有剩余 fd，保证 file reference 只释放一次且不在 active stack/active CR3/IRQ unsafe path 销毁 backing state

## 3. syscall 接入

- [x] 3.1 扩展 `include/bigos/syscall.h` 和 syscall dispatch，增加 `SYS_OPEN`、`SYS_READ`、`SYS_CLOSE`，保持 `int 0x80` vector、register ABI、EOI 规则和未知 syscall 错误语义不变
- [x] 3.2 实现 `open` syscall 的用户 path 校验和 bounded copy，要求 NUL 结尾、长度上限、绝对路径和 read-only flags，成功返回当前 process fd
- [x] 3.3 实现 `read` syscall 的用户 buffer 校验和 copy-out，按 fd table + VFS file offset 读取，返回 byte count 或确定性负错误码
- [x] 3.4 实现 `close` syscall，释放当前 process fd table entry，处理 bad fd、already closed 和非进程上下文错误
- [x] 3.5 为 fd/VFS syscall 增加 blocking-context guard，禁止 IRQ、preemption-disabled scheduler critical section 和其他不可阻塞上下文执行可能分配或同步磁盘读取的路径

## 4. smoke 迁移和文档

- [x] 4.1 迁移现有 `fs_smoke` 到 VFS open/read/close 路径，不新增 `fd_vfs_smoke` 开关，通过读取 `/boot/fs_smoke.txt` 输出 deterministic pass/fail COM1 marker
- [x] 4.2 更新 `user_elf_smoke` 或相关 kernel-only consumer，使至少一个 ELF/image 文件读取路径可选择复用 VFS/fd read 语义，同时保留原 exFAT fallback 或清晰回滚策略
- [x] 4.3 更新 runtime smoke matrix，记录 fd/VFS、open/read/close、bad-fd、EOF、not-found、invalid-user-buffer、exec inheritance 和 exit/reap close-all 覆盖范围
- [x] 4.4 更新 `docs/en` canonical 文档，说明fd/VFS shell boundary fd/VFS syscall ABI、VFS 边界、fd 生命周期、非目标和验证方法
- [x] 4.5 同步更新 `docs/zh` 对应 Markdown 路径，保持与 `docs/en` 技术事实和相对路径一致
- [x] 4.6 检查 `roadmap.md` 是否需要记录 `introduce-fd-vfs-shell` 状态和后续阶段依赖，避免与userland runtime baseline/VMA 或 userland runtime 建议冲突

## 5. 源码检查和验证

- [x] 5.1 增加或更新 source-level checks，覆盖 VFS root 初始化、open 成功/失败、fd table capacity、bad fd、double close、read EOF clamp、offset advancement、exec inheritance 和 exit/reap close-all
- [x] 5.2 运行 `openspec validate introduce-fd-vfs-shell --strict`，修复 proposal/spec/tasks 格式、delta requirement 和场景语法问题
- [x] 5.3 运行最窄有用的 `xmake` cross-toolchain build；若 `xmake`、`x86_64-elf-gcc`、`x86_64-elf-g++` 或 binutils 不可用，记录 blocker、替代检查和残余风险
- [x] 5.4 对新增或修改的 C++ 源/头文件运行接近 freestanding C++17 x86_64 交叉环境的 clang/clangd 辅助静态检查；若 flags、compile database 或工具不可用，记录 gap、历史诊断和当前变更风险
- [x] 5.5 使用 QEMU headless serial-marker smoke 验证 fd/VFS marker；涉及 ATA PIO、port-IO、IRQ/timer/blocking 或 user syscall 行为时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证
- [x] 5.6 将 validation notes 分离记录 passed checks、skipped checks 与原因、historical diagnostics、current-change diagnostics、emulator/toolchain blockers 和 residual bootability risk
