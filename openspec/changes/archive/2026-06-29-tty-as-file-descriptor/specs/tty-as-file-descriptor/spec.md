# tty-as-file-descriptor 规格增量

本规格定义把全局终端表达为标准 `vfs::File` 句柄、将标准描述符 fd 0/1/2 安装为终端 File、并把终端读写从裸 fd 特例收编到统一 `file->ops` 派发的可观测行为。所有 requirement 以内核内可观测行为或 headless 仿真器输出表达，不引入完整 POSIX tty、多路复用 syscall 或非阻塞标志。

## ADDED Requirements

### Requirement: 终端表达为设备/句柄两层

内核 SHALL 把终端拆分为“设备层”与“句柄层”：设备层为长期存在的全局终端状态（输入环与等待队列），句柄层为 per-open 的 `vfs::File`，其 `private_data` 指向设备层。句柄层的创建与释放 MUST NOT 释放或失效设备层状态。

#### Scenario: 句柄释放不影响设备层

- **WHEN** 一个终端 `vfs::File` 句柄的引用计数归零并被释放
- **THEN** 仅释放该 File 句柄结构本身，全局终端输入环与等待队列保持有效，后续仍能从终端读取输入

### Requirement: 标准描述符安装为终端 File

进程创建时内核 SHALL 把一个可读且可写的终端 `vfs::File` 安装到标准描述符 fd 0、fd 1、fd 2。这三个标准描述符 MUST 指向同一个终端 File 句柄，并相应增加其引用计数。

#### Scenario: 新进程的标准描述符指向终端 File

- **WHEN** 一个用户进程被创建并完成 fd 表初始化
- **THEN** fd 0、fd 1、fd 2 均存在已安装的 `vfs::File`，且通过 fd 查询文件对象时三者返回同一终端 File

#### Scenario: 标准描述符共享引用计数

- **WHEN** 关闭 fd 0、fd 1、fd 2 中的部分描述符
- **THEN** 仅当三者全部关闭后该终端 File 句柄才被释放，期间仍可通过未关闭的标准描述符读写终端

### Requirement: 终端读写经统一文件操作派发

终端读写 SHALL 通过 fd 表的 `file->ops` 派发，而不再依赖针对裸 fd 0/1/2 的特例分支。终端文件操作的读 MUST 复用既有终端阻塞读语义（规范模式与原始模式），写 MUST 复用既有默认控制台写出语义。

#### Scenario: 读取标准输入经 ops 派发

- **WHEN** 用户程序对 fd 0 发起读取且终端输入环暂无可消费输入
- **THEN** 读取经 `file->ops` 进入既有终端阻塞读路径，在终端等待队列上阻塞，直到有输入到达、超时或上下文不可阻塞，行为与既有终端读一致

#### Scenario: 写出标准输出经 ops 派发

- **WHEN** 用户程序对 fd 1 或 fd 2 发起写出
- **THEN** 写出经 `file->ops` 进入既有默认控制台写出路径，可见控制台输出与既有行为一致

### Requirement: 保留 headless 验证标记

终端写出路径 SHALL 原样保留既有 headless 验证所依赖的 COM1 标记（包括 `BIGOS_USER_WRITE_SYSCALL` 标记与随后的内容输出）及其顺序与长度上限约束。收编裸 fd 特例 MUST NOT 改变这些标记的文本、出现时机或默认启动的可见输出行为。

#### Scenario: 默认启动仍输出既有写标记

- **WHEN** 在默认启动配置下通过 QEMU headless 路径运行，用户程序写出到标准输出
- **THEN** COM1 仍按既有顺序输出 `BIGOS_USER_WRITE_SYSCALL` 标记与对应内容，默认启动可见行为与既有基线一致

### Requirement: 终端句柄遵循标准引用计数与复制语义

终端 `vfs::File` 句柄 SHALL 遵循与其它文件句柄一致的 `retain`/`release` 引用计数语义；其关闭操作对设备层 MUST 为无操作。`fork` 复制 fd 表、`dup`/`dup2` 复制描述符时 MUST 按既有共享语义对终端句柄增加引用计数，且 MUST NOT 重复释放设备层状态。

#### Scenario: fork 后父子共享终端句柄

- **WHEN** 一个持有标准终端描述符的进程 `fork`
- **THEN** 子进程的标准描述符与父进程共享同一终端 File 句柄且引用计数相应增加，父子任一方关闭描述符都不会使另一方的终端读写失效

#### Scenario: 终端关闭不重复释放设备层

- **WHEN** 进程退出或显式关闭其全部终端描述符
- **THEN** 终端 File 句柄在引用计数归零时被释放，设备层全局终端状态未被释放，且不发生重复释放

### Requirement: 终端句柄经 fstat 报告为字符设备

终端 `vfs::File` 句柄的 `fstat`（`SYS_FSTAT`）SHALL 报告字符设备类型与字符设备 mode（`S_IFCHR`），使用户态可经既有 `fstat` ABI 区分终端与普通文件/pipe，而不新增 syscall 编号。普通文件与 pipe MUST NOT 被报告为字符设备。依赖"标准 fd 是否裸 fd"判断交互性的消费者（如 `/bin/sh`）SHALL 改用基于 `fstat` 的字符设备判断（`isatty`），使重定向到普通文件或 pipe 的标准 fd 被判定为非终端。

#### Scenario: 终端 fd 经 fstat 识别为字符设备

- **WHEN** 用户程序对持有终端句柄的 fd 0/1/2 调用 `fstat`
- **THEN** 返回的元数据类型为字符设备且 mode 含 `S_IFCHR`，据此 `isatty` 返回真

#### Scenario: 重定向后的标准 fd 不再被判为终端

- **WHEN** 标准 fd 被重定向到普通文件或 pipe 后调用 `fstat`/`isatty`
- **THEN** 该 fd 不报告为字符设备，`isatty` 返回假，依赖该判断的交互行为（如 shell 提示符）随之关闭
