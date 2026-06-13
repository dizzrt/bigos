## ADDED Requirements

### Requirement: 可写后端支持受限常规文件 rename

BigOS SHALL 在 RAM-backed `/rw` 可写运行时文件系统内支持受限常规文件 `rename`。该操作 MUST 只在同一可写后端内移动或改名常规文件目录项，MUST 经权限、路径长度、父目录存在性、对象类型、容量和目标状态检查后再提交目录项更新，MUST 保持运行期一致性，且 MUST NOT 引入硬链接、符号链接、跨挂载 rename、完整目录 rename、完整 POSIX atomic replacement、journaling 或跨重启持久化承诺。

#### Scenario: 常规文件重命名成功

- **WHEN** 调用方在 `/rw` 内对一个存在的常规文件执行 rename，且源父目录和目标父目录可写、目标名称不存在、容量充足
- **THEN** BigOS MUST 让目标路径在同一运行期内可查找并指向原文件内容
- **AND** 源路径 MUST 不再可查找

#### Scenario: 同一父目录同一名称返回 no-op

- **WHEN** 调用方在 `/rw` 内执行 rename，且源路径和目标路径解析为同一父目录下的同一目录项名称
- **THEN** BigOS MUST 返回成功
- **AND** MUST NOT 修改目录项、inode 元数据、文件数据、fd 引用或 open file offset

#### Scenario: 目标已存在时保守失败

- **WHEN** 调用方把 `/rw` 常规文件 rename 到一个已存在目标路径，且源目标不是同一父目录同一名称
- **THEN** BigOS MUST 返回确定性 `-EEXIST` 或等价目标已存在错误
- **AND** MUST NOT 移除源目录项，MUST NOT 修改目标对象

#### Scenario: 不支持对象和跨后端被拒绝

- **WHEN** 调用方尝试 rename 目录、只读 exFAT 路径、跨挂载路径、缺失源路径、缺失目标父目录或不支持对象类型
- **THEN** BigOS MUST 返回确定性错误
- **AND** MUST NOT 修改 `/rw`、只读 boot assets、exFAT 发现状态或无关目录项

#### Scenario: 失败不发布半成品目录项

- **WHEN** rename 因权限、容量、IO、非法路径或内部状态检查失败
- **THEN** BigOS MUST 保持源路径、目标路径、inode 元数据和文件数据处于失败前的可解释状态
- **AND** MUST NOT panic，内核态 fault 诊断路径除外

### Requirement: rename 后 open file 引用保持稳定

BigOS SHALL 在 rename 改变目录项名称时保持已打开文件对象的引用生命周期稳定。已打开 fd MUST 继续引用原 inode/data blocks；rename 成功后新的路径查找 MUST 看到目标名称，源名称 MUST 不再可见；仍有 open fd 时不得提前释放对应文件对象、inode 或数据块。

#### Scenario: rename 已打开文件后 fd 仍有效

- **WHEN** 进程打开 `/rw` 常规文件后对其路径执行 rename
- **THEN** 已打开 fd MUST 在关闭前继续按打开权限访问同一文件内容
- **AND** 新目标路径的后续 open/read MUST 观察同一运行期文件内容

#### Scenario: rename 不改变 dup 后 fd 共享关系

- **WHEN** 进程 dup 一个文件 fd 后对该文件路径执行 rename
- **THEN** dup 后 fd MUST 继续共享同一 open file object 和 offset
- **AND** rename MUST NOT 破坏 fd 引用计数或进程退出/reap 时的关闭规则
