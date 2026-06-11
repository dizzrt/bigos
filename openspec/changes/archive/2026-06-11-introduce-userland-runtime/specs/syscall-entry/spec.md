## ADDED Requirements

### Requirement: SYS_EXECVE 用户态镜像替换 syscall

BigOS SHALL 在 `int 0x80` ABI 末尾以 append-only 方式新增 `SYS_EXECVE`，把内核内已有的当前进程镜像替换路径（`exec_current_from_elf_image` + VFS 读路径）暴露给 CPL3。`SYS_EXECVE` MUST 接受用户态参数：可执行文件 `path`、`argv`（NULL 结尾指针数组）与 `envp`；MUST 经现有 VMA-backed 用户缓冲区校验拷入内核后再使用；成功时以新镜像替换当前进程地址空间并进入新程序入口、原调用点不返回；失败时 MUST 返回确定性负 errno（如 `-ENOENT`/`-EACCES`/`-ENOEXEC`/`-EFAULT`/`-E2BIG`/`-ENOMEM`）。`SYS_EXECVE` 在分配或进入同步块 IO 之前 MUST 检查调度阻塞守卫。新增此号 MUST NOT 改动既有 syscall 号位、寄存器约定、向量/DPL 布局或「syscall 不发 EOI」规则。

#### Scenario: execve 成功替换镜像

- **WHEN** 用户进程以合法 `path`/`argv`/`envp` 调用 `SYS_EXECVE` 且目标为可加载 ELF64 `ET_EXEC`
- **THEN** 内核 MUST 经现有 ELF 装载路径用新镜像替换当前进程地址空间并进入新程序入口
- **AND** 该 syscall MUST 不返回到原调用点

#### Scenario: execve 失败返回确定性负 errno

- **WHEN** `SYS_EXECVE` 的目标不存在、不可加载、参数非法或用户缓冲区校验失败
- **THEN** 内核 MUST 返回确定性负 errno 且 MUST 保持当前进程镜像不被破坏
- **AND** 调用进程 MUST 能从该失败返回继续执行

#### Scenario: execve 用户缓冲校验与阻塞守卫

- **WHEN** `SYS_EXECVE` 读取用户态 `path`/`argv`/`envp` 或进入同步块 IO/分配
- **THEN** 内核 MUST 先经 VMA-backed 用户缓冲区校验拷入相关数据
- **AND** 内核 MUST 在分配或进入同步块 IO 前检查调度阻塞守卫

#### Scenario: append-only 不改动既有 ABI

- **WHEN** 新增 `SYS_EXECVE`
- **THEN** 既有 syscall 号位、寄存器参数/返回约定、`VECTOR_SYSCALL = 0x80`、DPL 设置与「syscall 不发 EOI」MUST 保持不变
- **AND** `SYS_EXECVE` MUST 取 ABI 末尾的新号位而不复用或重排既有号位
