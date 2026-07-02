## MODIFIED Requirements

### Requirement: 最小 UDP socket 创建与 fd 集成
BigOS SHALL provide a user-visible minimal UDP socket that is represented as a kernel-internal `vfs::File` backend and installed into the existing per-process fd table. socket 创建 MUST 校验请求的 domain/type/protocol：属于 BigOS 有界 UDP 子集（IPv4 datagram）时创建 datagram socket backend；属于 BigOS 有界 TCP 子集（IPv4 `SOCK_STREAM` + TCP）时创建 stream socket backend（见 `stream-socket-interface`）。成功时返回一个进程本地 fd，并复用既有 fd 分配、`dup`/`dup2`、`close` 与 `close-on-exec` 路径。该 socket fd MUST NOT 改变既有 syscall number、pipe、VFS 或文件 fd 的行为，且 datagram 与 stream socket MUST 使用各自独立的 backend ops 表以区分类型。

#### Scenario: 创建有界 UDP socket
- **WHEN** 用户程序以 BigOS 有界 UDP 子集（IPv4 datagram）参数调用 socket 创建 syscall，且 fd 表与协议端口表均有可用容量
- **THEN** BigOS MUST 创建一个 datagram socket backend `vfs::File`，把它安装到最低可用 fd 并返回该 fd
- **AND** 该 fd MUST 可被既有 `close`、`dup`、`dup2`、`fcntl`（`close-on-exec`）路径处理

#### Scenario: 创建有界 stream socket
- **WHEN** 用户程序以 BigOS 有界 TCP 子集（IPv4 `SOCK_STREAM` + TCP protocol）参数调用 socket 创建 syscall，且 fd 表有可用容量
- **THEN** BigOS MUST 创建一个 stream socket backend `vfs::File`（独立于 datagram ops 表），把它安装到最低可用 fd 并返回该 fd

#### Scenario: 拒绝不支持的 socket 参数
- **WHEN** 用户程序请求既不属于有界 UDP 子集也不属于有界 TCP 子集的 domain、type 或 protocol（例如未支持的地址族或未支持的 socket 类型）
- **THEN** BigOS MUST 以确定性负 errno 拒绝创建，且 MUST NOT 安装任何 fd

#### Scenario: 容量耗尽
- **WHEN** 进程 fd 表已达上限或协议层 UDP 端口/资源已无可用容量
- **THEN** BigOS MUST 返回确定性负 errno（fd 表满与协议资源满使用各自确定性的错误码）
- **AND** 失败路径 MUST NOT 泄漏部分创建的 `vfs::File` 或内核内部 endpoint

### Requirement: socket fd 生命周期与 read/write 语义
BigOS SHALL integrate socket fds with the existing fd lifecycle，使 socket 在 `close`、`fork`、`close-on-exec` 与引用计数路径下行为确定。datagram（UDP）socket 的 `read`/`write` ops MUST 不充当隐式无地址收发：对 datagram socket fd 的 `read`/`write` MUST 返回确定性不支持错误，收发只通过 `sendto`/`recvfrom`。stream（TCP）socket 的 `read`/`write` MUST 走有序字节流路径（见 `stream-socket-interface`），而对 stream socket 的 `sendto`/`recvfrom` MUST 返回确定性不支持错误。两类 socket 的类型区分 MUST 通过各自 backend ops 表身份判定。

#### Scenario: 关闭释放 socket 资源
- **WHEN** socket fd 的最后一个引用被 `close`（或进程退出统一回收）
- **THEN** BigOS MUST 通过既有 `vfs::release` 路径调用 socket backend 的 close，回收对应内核内部 endpoint/连接控制块，且 MUST NOT 重复释放或泄漏

#### Scenario: fork 后共享 socket 引用
- **WHEN** 持有 socket fd 的进程 `fork`
- **THEN** 子进程 MUST 通过既有 `vfs::retain` 引用计数共享同一 socket `vfs::File`/endpoint/连接
- **AND** 任一进程关闭其 fd MUST 只递减引用计数，仅在最后一次释放时回收 endpoint/连接控制块

#### Scenario: datagram socket read/write 返回不支持
- **WHEN** 用户程序对 datagram（UDP）socket fd 直接调用 `read` 或 `write` 系统调用
- **THEN** BigOS MUST 返回确定性不支持错误码，且 MUST NOT 进行隐式网络收发

#### Scenario: stream socket sendto/recvfrom 返回不支持
- **WHEN** 用户程序对 stream（TCP）socket fd 调用 `sendto` 或 `recvfrom`
- **THEN** BigOS MUST 返回确定性不支持错误码，且 MUST NOT 进行 datagram 收发
