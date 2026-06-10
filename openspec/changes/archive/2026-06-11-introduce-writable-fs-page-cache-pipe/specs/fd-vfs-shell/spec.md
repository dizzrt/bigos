## ADDED Requirements

### Requirement: VFS 可写 open 与写/lseek 文件操作

BigOS SHALL 扩展 fd/VFS 壳层以支持可写文件操作：`FileOperations` MUST 新增 `write` 与 `lseek` 操作（追加，不破坏既有 `read`/`close` 与 `File` 既有字段布局），`open_absolute` MUST 接受可写/创建 flags 与 `O_CREAT` 时的 mode/owner 入参，`vfs::Status` MUST 覆盖只读后端拒写、无空间与权限拒绝等失败。只读后端的 `write` MUST 返回拒写错误，且现有只读 open/read/close 与只读 exFAT 行为 MUST 保持不变。

#### Scenario: 可写 open 创建带 owner/mode 的文件

- **WHEN** 调用方以可写/创建 flags 与 mode 通过 `open_absolute` 打开可写后端的路径
- **THEN** VFS MUST 经可写后端创建/打开可写文件对象，记录 owner 为调用进程身份、采用传入 mode，并将该文件对象标记为可写

#### Scenario: 文件 write 经后端写入并推进 offset

- **WHEN** 调用方对可写打开文件对象调用 VFS `write`
- **THEN** VFS MUST 经可写后端与块缓冲缓存写入数据、按成功字节推进 offset，失败时不推进 offset 并返回确定性错误

#### Scenario: lseek 校验溢出与不可定位对象

- **WHEN** 调用方对打开文件对象调用 VFS `lseek`
- **THEN** VFS MUST 对可定位文件返回校验过溢出的新 offset，对管道等不可定位对象返回 `-ESPIPE`，对非法 whence/溢出返回 `-EINVAL`

#### Scenario: 只读后端拒写

- **WHEN** 调用方对只读 exFAT 后端的文件对象调用 VFS `write` 或以写 flags 打开
- **THEN** VFS MUST 返回 `-EROFS` 并将其映射为对应 `vfs::Status`，MUST NOT 修改文件系统状态

### Requirement: fd 表支持管道与 dup/dup2

BigOS SHALL 让进程 fd 表支持指向管道端的打开文件对象与 `dup`/`dup2` 复制。`dup`/`dup2` 复制的 fd MUST 指向同一打开文件对象并共享 offset 与底层引用计数，`dup2` MUST 在目标 fd 已打开时先关闭它，引用计数 MUST 保证每个 fd 关闭时底层对象引用精确递减一次。

#### Scenario: dup 共享同一打开文件对象

- **WHEN** 进程对一个有效 fd 调用 `dup`/`dup2`
- **THEN** fd 表 MUST 把新 fd 绑定到同一打开文件对象、增加其引用计数，并令两个 fd 共享同一 offset

#### Scenario: 关闭共享 fd 精确递减引用

- **WHEN** 多个 fd 共享同一打开文件对象，其中一个 fd 被 close
- **THEN** fd 表 MUST 仅移除该 fd 项并把底层对象引用递减一次，仅在引用归零时释放底层对象

#### Scenario: 管道端 fd 纳入 exec 继承与退出关闭

- **WHEN** 进程 `exec` 或退出/被回收，且其 fd 表含管道端 fd
- **THEN** fd 表 MUST 按 close-on-exec 规则在 exec 时关闭或保留管道端 fd，并在退出/回收时关闭所有剩余管道端 fd 各一次
