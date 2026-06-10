## ADDED Requirements

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
