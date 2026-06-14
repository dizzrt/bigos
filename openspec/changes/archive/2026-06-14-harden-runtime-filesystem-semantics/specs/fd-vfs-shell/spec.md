## ADDED Requirements

### Requirement: fd/VFS 运行时错误映射稳定

BigOS SHALL make fd/VFS path-taking and fd-taking runtime filesystem operations return stable user-visible negative errno values across VFS dispatch, backend failures, syscall return paths, libc wrappers, and shell-visible errors. The mapping MUST cover missing paths, existing targets, invalid object types, read-only backend writes, permission denial, capacity exhaustion, invalid descriptors, illegal user buffers, unsupported seek/enumeration targets, caller output buffer exhaustion, backend IO failures, and nonblocking-context rejection. Directory enumeration MUST map caller output capacity exhaustion to `-ERANGE`, illegal enumeration arguments to `-EINVAL`, and backend capacity exhaustion to `-ENOSPC` or the documented backend capacity error. The mapping MUST NOT expose internal backend enum names as user ABI.

#### Scenario: 只读后端写请求返回稳定错误

- **WHEN** 用户进程对只读 exFAT 路径发起创建、写入、截断、删除或 rename 请求
- **THEN** fd/VFS MUST 返回稳定的 read-only filesystem 错误
- **AND** syscall/libc/shell 观察到的错误 MUST 与等价只读后端操作一致
- **AND** fd/VFS MUST NOT 修改只读 filesystem、raw image、boot assets、fd table 或 cwd 状态

#### Scenario: 不同失败点映射为确定性 errno

- **WHEN** 文件操作在路径解析、fd 查找、用户缓冲校验、权限检查、容量检查、对象类型检查或 backend IO 阶段失败
- **THEN** fd/VFS MUST 返回对应确定性负 errno
- **AND** 失败路径 MUST NOT 依赖未初始化输出、内部枚举值或调试字符串供用户态判定

#### Scenario: 目录枚举缓冲不足返回 ERANGE

- **WHEN** 用户进程对有效目录 fd 执行最小目录枚举，但调用方提供的输出容量不足以容纳需要返回的有界目录项结果
- **THEN** fd/VFS MUST return `-ERANGE`
- **AND** MUST NOT report the condition as backend space exhaustion
- **AND** MUST NOT modify directory state, unrelated fd state, or publish partial uninitialized directory entries

#### Scenario: 不可阻塞上下文拒绝文件系统副作用

- **WHEN** fd/VFS 操作会执行分配、等待、同步块 IO、缓存落盘或目录项变更，但调用上下文为 IRQ、调度临界区、preemption-disabled 区域或其它不可阻塞路径
- **THEN** BigOS MUST 确定性失败或进入文档化诊断路径
- **AND** MUST NOT 发布 fd、修改目录项、推进 offset、执行阻塞 IO 或等待队列操作

### Requirement: fd/VFS 后端分派差异可观察

BigOS SHALL dispatch runtime filesystem operations according to the resolved backend and object type, preserving the difference between read-only exFAT and RAM-backed writable `/rw`. The same cwd-relative or absolute path operation MUST produce equivalent behavior after resolving to the same backend target. Backend dispatch MUST remain bounded and MUST NOT introduce mount namespaces, symlink traversal, broad file-backed mappings, async IO, or full POSIX VFS semantics.

#### Scenario: cwd 相对路径保持后端差异

- **WHEN** 当前进程 cwd 位于只读 exFAT 路径或 `/rw` 路径下，并以相对路径执行 open、write、mkdir、unlink、rename、stat 或目录枚举
- **THEN** fd/VFS MUST 先按共享路径解析契约得到目标
- **AND** MUST 按解析后的后端应用只读拒写或 `/rw` 可写语义

#### Scenario: 跨后端目录变更被拒绝

- **WHEN** 用户进程尝试在只读 exFAT 与 `/rw` 之间执行 rename 或其它需要同一可写后端提交的目录变更
- **THEN** fd/VFS MUST 返回稳定的跨后端或不支持错误
- **AND** MUST NOT 修改任一后端状态、已打开 fd、cwd、缓存块或目录枚举结果

### Requirement: fd/VFS 运行时行为验证覆盖组合路径

BigOS SHALL provide behavior-oriented validation for fd/VFS runtime filesystem semantics across simple user programs and shell-visible operations. Validation MUST cover success and failure paths for read-only exFAT, RAM-backed `/rw`, path and fd operations, directory object handling, open file references, and deterministic errno reporting. Environment-dependent emulator checks MUST record missing QEMU, Bochs, cross toolchain, ROM/display, raw image, serial oracle, or timeout dependencies as skipped rather than passed.

#### Scenario: 用户态观察成功和失败路径

- **WHEN** runtime filesystem semantics validation runs with required build and emulator support
- **THEN** it MUST exercise at least one read-only file success path, one `/rw` create/write/read/stat/list path, and representative failure paths for read-only write, missing path, invalid fd, permission denial, and illegal object type
- **AND** the observed errno and file state MUST match the fd/VFS contract

#### Scenario: 环境不可用时记录跳过

- **WHEN** emulator, cross toolchain, disk image, display/ROM, serial oracle, or timeout dependencies are unavailable
- **THEN** validation notes MUST record the unavailable dependency and residual risk
- **AND** they MUST NOT claim fd/VFS runtime semantics validation passed
