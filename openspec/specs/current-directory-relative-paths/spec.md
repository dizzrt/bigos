## Purpose

定义 BigOS 有界 current directory 与相对路径能力：为每个用户进程维护 cwd，在 VFS 路径解析层统一处理绝对路径和相对路径，暴露 `chdir`/`getcwd`，并让 libc、shell 和小型用户工具可观察该行为。该能力支持当前目录树内的 POSIX-style `.`/`..` 组件语义，但不引入 mount namespace、`chroot`、符号链接遍历、稳定 inode 身份或完整 POSIX pathname canonicalization。

## Requirements

### Requirement: 每进程 current directory 状态
BigOS SHALL 为每个用户进程维护一个有界 current directory 状态。该状态 MUST 表示为当前 VFS 可解析的绝对目录路径，默认进程 cwd MUST 为 root，路径长度 MUST 受系统路径上限约束，且 cwd 状态 MUST 随进程对象生命周期初始化、复制和释放。该能力 MUST NOT 引入 mount namespace、`chroot`、符号链接遍历、稳定 inode 身份或完整 POSIX pathname canonicalization。

#### Scenario: 新进程默认 cwd 为 root
- **WHEN** BigOS 创建一个没有父进程 cwd 来源的初始用户进程
- **THEN** 该进程的 cwd MUST 初始化为 `/`
- **AND** 后续相对路径解析 MUST 以该 cwd 作为起点

#### Scenario: cwd 状态有界
- **WHEN** 内核创建、复制或更新进程 cwd
- **THEN** cwd 路径 MUST 是 NUL-terminated 且不超过系统路径上限的绝对目录路径
- **AND** 分配或长度校验失败 MUST 返回确定性错误并避免发布部分更新的 cwd

### Requirement: cwd 继承和 exec 保留
BigOS SHALL 定义 cwd 在进程创建、`fork`、`execve`、退出和回收中的有界生命周期。`fork` 子进程 MUST 继承父进程提交后的 cwd 值，`execve` MUST 保留当前进程 cwd，退出和 safe reap MUST 释放 cwd 相关资源。该语义 MUST NOT 暗示完整 POSIX process model、session、process group 或 namespace 行为。

#### Scenario: fork 继承 cwd
- **WHEN** 父进程在 cwd 为 `/rw/work` 时成功 `fork`
- **THEN** 子进程 MUST 观察到相同的 cwd 值
- **AND** 父子后续 `chdir` MUST 只改变各自进程的 cwd

#### Scenario: execve 保留 cwd
- **WHEN** 进程在非 root cwd 下成功 `execve` 一个支持的静态用户 ELF
- **THEN** 新用户镜像 MUST 继续使用 exec 前的 cwd 进行相对路径解析
- **AND** exec 失败且旧镜像仍可运行时 MUST 不改变旧 cwd

#### Scenario: 进程回收释放 cwd
- **WHEN** 进程退出、fault 终止并到达 safe reaper 边界
- **THEN** BigOS MUST 释放该进程拥有的 cwd 资源
- **AND** MUST NOT 在 IRQ、当前活动栈 teardown 或不可阻塞上下文中释放不安全资源

### Requirement: chdir 和 getcwd 用户可见契约
BigOS SHALL 提供有界 `chdir` 和 `getcwd` 能力。`chdir` MUST 解析目标路径并确认目标是已存在目录后再更新当前进程 cwd；`getcwd` MUST 把当前 cwd 复制到有效用户缓冲区。失败 MUST 返回确定性负 errno 并保持旧 cwd 不变；用户缓冲区容量不足 MUST 返回 `-ERANGE`，无效用户缓冲区 MUST 返回用户内存相关错误，且 MUST NOT 泄露未初始化内核内存。

#### Scenario: chdir 到已存在目录
- **WHEN** 用户进程调用 `chdir` 指向一个已存在的目录路径
- **THEN** BigOS MUST 将当前进程 cwd 更新为该目录的有界绝对路径
- **AND** 后续相对路径操作 MUST 以新 cwd 为起点

#### Scenario: chdir 失败不改变 cwd
- **WHEN** 用户进程调用 `chdir` 指向缺失对象、普通文件、过长路径或非法路径形式
- **THEN** BigOS MUST 返回确定性错误
- **AND** 当前进程 cwd MUST 保持为调用前的值

#### Scenario: getcwd 拷贝当前路径
- **WHEN** 用户进程以足够大的有效用户缓冲区调用 `getcwd`
- **THEN** BigOS MUST 将当前 cwd 的 NUL-terminated 路径复制到用户缓冲区
- **AND** 返回值 MUST 让 libc wrapper 能区分成功与失败

#### Scenario: getcwd 拒绝无效缓冲区
- **WHEN** 用户进程以 unmapped、只读或 kernel 地址范围缓冲区调用 `getcwd`
- **THEN** BigOS MUST 返回用户内存相关的确定性错误
- **AND** MUST NOT 写入部分未验证用户内存或泄露未初始化数据

#### Scenario: getcwd 缓冲区过小返回 ERANGE
- **WHEN** 用户进程以有效但容量不足以容纳 cwd 和 NUL terminator 的缓冲区调用 `getcwd`
- **THEN** BigOS MUST 返回 `-ERANGE`
- **AND** 当前进程 cwd MUST 保持不变

### Requirement: 相对路径解析规则
BigOS SHALL 在 VFS 路径解析层统一处理绝对路径和相对路径。绝对路径 MUST 从 root 解析；相对路径 MUST 从当前进程 cwd 解析；路径组件 MUST 支持 POSIX-style `.` 和 `..` 语义，其中 `.` 指向当前目录，`..` 指向父目录，root 的父目录仍为 root。空路径、超过路径上限、非 NUL-terminated 用户路径、跨越支持边界的路径形式和不支持的组件语义 MUST 确定性失败。第一版 MUST NOT 承诺符号链接遍历、mount namespace、`chroot` 或完整 `realpath` 语义。

#### Scenario: 相对路径从 cwd 解析
- **WHEN** 当前进程 cwd 为 `/rw/work` 且用户打开相对路径 `note.txt`
- **THEN** VFS MUST 按 `/rw/work/note.txt` 的等价目标执行查找
- **AND** 成功或失败结果 MUST 与该目标在对应后端中的状态一致

#### Scenario: 绝对路径不受 cwd 影响
- **WHEN** 当前进程 cwd 为 `/rw/work` 且用户打开绝对路径 `/boot/user/init.elf`
- **THEN** VFS MUST 从 root 解析该绝对路径
- **AND** cwd MUST NOT 改变绝对路径目标

#### Scenario: dot-dot 返回父目录
- **WHEN** 当前进程 cwd 为 `/rw/work/sub` 且用户访问相对路径 `../note.txt`
- **THEN** VFS MUST 按 `/rw/work/note.txt` 的等价目标执行查找
- **AND** 中间组件若不是目录 MUST 返回确定性错误

#### Scenario: root 的父目录保持 root
- **WHEN** 当前进程 cwd 为 `/` 且用户访问相对路径 `../boot/user/init.elf`
- **THEN** VFS MUST treat root's parent as root and resolve the target as `/boot/user/init.elf`
- **AND** MUST NOT escape above the VFS root

#### Scenario: 不支持路径形式确定性失败
- **WHEN** 用户提交空路径、过长路径、未终止路径或超出第一版组件语义的路径
- **THEN** BigOS MUST 返回确定性错误
- **AND** MUST NOT 修改进程 cwd、fd table 或文件系统状态

### Requirement: cwd 行为验证
BigOS SHALL 提供分层验证路径覆盖 cwd 与相对路径行为。验证 MUST 覆盖内核路径解析、POSIX-style `.`/`..` 组件语义、`fork` 继承、`execve` 保留、`chdir`/`getcwd` 成功和失败、`getcwd` 小缓冲 `ERANGE`、libc wrapper、shell `cd`、`/bin/pwd` 消费，以及只读 exFAT 与 `/rw` 后端的相对路径差异。依赖 QEMU、Bochs、交叉工具链、ROM/display 或磁盘镜像配置的检查不可用时 MUST 记录跳过原因和剩余风险。

#### Scenario: 行为验证覆盖继承和解析
- **WHEN** cwd runtime validation 在已配置环境中运行
- **THEN** 验证 MUST 观察至少一个 `chdir` 后相对路径成功、`..` 组件解析、`fork` 子进程继承 cwd、`execve` 后 cwd 保留和 `/bin/pwd` 输出的组合路径
- **AND** 结果 MUST 可由用户程序输出、退出状态、串口日志或确定性 runtime signal 判断

#### Scenario: 环境不可用时记录跳过
- **WHEN** 本地缺少 emulator、交叉工具链、显示/ROM 依赖、raw image 或超时 oracle
- **THEN** 对应 runtime 验证 MAY 被跳过
- **AND** 跳过记录 MUST 明确缺失条件、替代检查和剩余风险
