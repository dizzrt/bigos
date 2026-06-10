## Purpose

定义 BigOS 进程身份与最小权限能力：在 `Process` 中维护最小身份四元组 uid/gid/euid/egid 与进程墙钟时间戳；定义 init root 身份、ELF 创建默认 root、fork 身份继承与 exec 不改变身份的规则；提供「调用方进程能否对目标进程执行特权操作」与「文件 owner/mode 访问判定」两类纯判定原语（供后续信号与可写文件系统阶段复用），并以非法输入一律拒绝为失败语义；并经默认关闭的验证开关验证。该能力只提供身份字段与判定原语，不接线任何强制点，不引入完整 POSIX 用户/组数据库、`setuid`/`setgid` 身份变更 syscall、setuid 位、login/认证或 supplementary groups。

## Requirements

### Requirement: 进程携带最小身份四元组

BigOS SHALL 为每个进程维护最小身份四元组 uid/gid/euid/egid，并定义其在 init、ELF 创建、fork 与 exec 各路径下的取值规则。

#### Scenario: init 进程以 root 身份启动

- **WHEN** PID 1 的 init 进程被创建
- **THEN** 其 uid/gid/euid/egid MUST 全部为 0（root）

#### Scenario: 非 fork 创建的进程默认 root

- **WHEN** 进程通过 ELF 创建路径（非 fork）产生且系统当前无 login/身份变更来源
- **THEN** 其 uid/gid/euid/egid MUST 默认为 0（root）

#### Scenario: fork 子进程继承父身份

- **WHEN** 父进程通过 `fork` 复制出子进程
- **THEN** 子进程的 uid/gid/euid/egid MUST 逐字段等于父进程对应值

#### Scenario: exec 不改变身份

- **WHEN** 进程通过 exec 用新镜像替换地址空间
- **THEN** 其 uid/gid/euid/egid MUST 保持不变（本阶段不实现 setuid 位）

### Requirement: 进程记录启动墙钟时间戳

BigOS SHALL 为每个进程记录其创建时刻的墙钟时间戳，基于墙钟能力提供的当前 Unix 秒。

#### Scenario: 进程创建记录时间戳

- **WHEN** 进程（init/ELF/fork）被创建
- **THEN** 其启动时间戳字段 MUST 取创建时刻的当前墙钟 Unix 秒
- **AND** exec 用新镜像替换地址空间时 MUST NOT 刷新该时间戳（exec 不是新进程）

### Requirement: 进程间特权操作判定原语

BigOS SHALL 提供一个纯函数，判定一个进程能否对另一个目标进程执行特权操作（未来 kill 的基础），root 放行任意目标，否则要求身份匹配，非法输入返回拒绝。

#### Scenario: root 可对任意目标执行

- **WHEN** 调用方进程的 euid 为 0（root），对任意目标进程做特权操作判定
- **THEN** 判定 MUST 返回允许

#### Scenario: 非 root 要求身份匹配

- **WHEN** 调用方进程 euid 非 0，对某目标进程做判定
- **THEN** 当且仅当调用方 euid 与目标进程身份匹配（如目标 uid 或 euid 相等）时判定 MUST 返回允许，否则 MUST 返回拒绝

#### Scenario: 非法输入返回拒绝

- **WHEN** 判定函数收到非法输入（如空进程指针）
- **THEN** 它 MUST 返回拒绝，并 MUST NOT panic 或产生副作用

### Requirement: 文件 owner/mode 访问判定原语

BigOS SHALL 提供权限位常量与一个纯函数，按 (file_uid, file_gid, mode, 请求方 uid/gid, 访问类型) 判定允许或拒绝，供后续可写文件系统阶段复用，root 全放行，非法输入返回拒绝。

#### Scenario: root 全放行

- **WHEN** 请求方 uid 为 0，对任意 owner/mode 的文件做访问判定
- **THEN** 判定 MUST 返回允许

#### Scenario: 按 owner/group/other 匹配权限位

- **WHEN** 请求方非 root，对带 owner/group/other 读写执行权限位的文件做某访问类型判定
- **THEN** 判定 MUST 在请求方为文件 owner 时检查 owner 位、为文件 group 时检查 group 位、否则检查 other 位
- **AND** 对应权限位置位时返回允许，否则返回拒绝

#### Scenario: 非法输入返回拒绝

- **WHEN** 判定函数收到非法访问类型或越界输入
- **THEN** 它 MUST 返回拒绝，并 MUST NOT panic 或产生副作用

### Requirement: 身份与权限能力经默认关闭开关验证

BigOS SHALL 通过默认关闭的验证开关与源码/行为断言验证身份继承与权限判定，且不改变默认启动 marker 与既有 smoke 矩阵。

#### Scenario: 身份/权限 smoke 发射有界 marker

- **WHEN** 启用 `time_identity_smoke`（`BIGOS_TIME_IDENTITY_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「init 为 root 且 fork 子进程继承身份」与「特权判定 root 放行、非匹配拒绝」并发射有界判定 marker（如 `BIGOS_TIME_IDENTITY_PASSED`/`BIGOS_TIME_IDENTITY_FAILED`）
- **AND** 该开关 MUST 默认关闭，默认启动 marker 与既有 smoke 矩阵 MUST 保持不变

### Requirement: may_signal 作为 kill 的强制点

BigOS SHALL 把现有纯判定原语 `bigos::cred::may_signal(actor, target)` 接成 `SYS_KILL` 信号投递的实际权限强制点，其判定语义保持不变（root 放行、否则身份匹配、非法输入拒绝），仅新增被实际调用的接线点。

#### Scenario: kill 强制点调用 may_signal

- **WHEN** 一个进程通过 `SYS_KILL` 向目标进程投递信号
- **THEN** 内核 MUST 调用 `may_signal(actor, target)` 判定权限，判定拒绝时 MUST NOT 投递并返回确定性 `-EPERM`
- **AND** `may_signal` 的判定逻辑（root 放行、euid 与目标 uid/euid 匹配放行、空指针/非法输入拒绝）MUST 保持不变

#### Scenario: root 可向任意目标投递

- **WHEN** 投递方进程 euid 为 0（root），对任意存在的目标进程投递合法信号
- **THEN** 权限判定 MUST 放行，内核 MUST 在目标 pending 位图置位该信号

#### Scenario: 非匹配身份投递被拒绝

- **WHEN** 投递方 euid 非 0 且与目标进程身份不匹配
- **THEN** 权限判定 MUST 拒绝，内核 MUST 返回 `-EPERM` 且不修改目标 pending 位图

### Requirement: may_access 作为可写文件系统的强制点

BigOS SHALL 把现有纯判定原语 `bigos::cred::may_access`（文件 owner/mode 访问判定）接成可写文件系统 open（写/创建）、`write`、`mkdir`、`unlink` 的实际访问权限强制点，其判定语义保持不变（root 全放行、按 owner/group/other 与访问类型匹配权限位、非法输入拒绝），仅新增被实际调用的接线点。只读后端的写请求 MUST 以 `-EROFS` 拒绝，权限判定拒绝 MUST 返回确定性 `-EACCES`。

#### Scenario: 写访问强制点调用 may_access

- **WHEN** 一个进程对可写后端的文件发起写打开、写入、创建或删除
- **THEN** 内核 MUST 调用 `may_access(file_uid, file_gid, mode, caller_uid, caller_gid, access_type)` 判定权限，判定拒绝时 MUST NOT 修改文件系统状态并返回确定性 `-EACCES`
- **AND** `may_access` 的判定逻辑（root 放行、按 owner/group/other 匹配权限位、非法输入拒绝）MUST 保持不变

#### Scenario: root 可写任意文件

- **WHEN** 发起方进程 uid 为 0（root），对任意 owner/mode 的可写后端文件发起写操作
- **THEN** 权限判定 MUST 放行，内核 MUST 继续执行写/创建/删除（受空间与只读后端约束）

#### Scenario: 非匹配身份写访问被拒绝

- **WHEN** 发起方非 root 且其 uid/gid 与文件 owner/group 及 other 权限位均不允许所请求的访问类型
- **THEN** 权限判定 MUST 拒绝，内核 MUST 返回 `-EACCES` 且不修改文件系统状态

#### Scenario: O_CREAT 新文件 owner 取调用方身份

- **WHEN** 进程以 `O_CREAT` 在可写后端创建新文件
- **THEN** 新文件 inode 的 owner uid/gid MUST 取调用进程身份、mode MUST 取调用方传入值，作为后续 `may_access` 判定的依据
