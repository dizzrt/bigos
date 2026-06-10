## ADDED Requirements

### Requirement: 可写文件系统后端与只读 exFAT 并存

BigOS SHALL 提供一个最小可写文件系统后端，与现有只读 exFAT 挂载并存且不改变只读 exFAT 的发现、挂载与读语义。可写后端 MUST 经块缓冲缓存读写其超级块、inode、目录项与数据块，MUST 维护文件 inode 的 owner（uid/gid）与 mode 元数据，MUST 有界（块大小、inode 数、文件大小、目录项数均有上限），且 MUST NOT 引入硬/软链接、`rename`、journaling、ACL/xattr 或文件 mmap。本阶段可写后端的默认承载介质 MUST 为 RAM-backed 块设备（不改动现有磁盘镜像/MBR/分区/exFAT 只读发现契约），其正确性语义只覆盖运行期一致性，MUST NOT 承诺跨重启持久化；磁盘分区承载 MUST 不在本阶段范围。

#### Scenario: 可写后端挂载且不影响只读 exFAT

- **WHEN** 内核初始化在块设备与缓存就绪后启用可写文件系统后端
- **THEN** 可写后端 MUST 在其挂载点（基于默认 RAM-backed 块设备）暴露可写根，且现有只读 exFAT 的挂载、路径查找与只读读 MUST 保持不变

#### Scenario: 只读后端拒绝写请求

- **WHEN** 调用方对只读 exFAT 后端的文件发起写、创建、截断或删除
- **THEN** BigOS MUST 返回确定性 `-EROFS`，MUST NOT 修改任何文件系统状态

#### Scenario: 承载介质初始化失败确定性降级

- **WHEN** 可写后端的 RAM-backed 承载介质分配失败
- **THEN** BigOS MUST 确定性地不发布可写挂载，MUST 保持只读 exFAT 路径不受影响，MUST NOT panic

### Requirement: 可写打开与文件创建

BigOS SHALL 支持以可写/创建 flags（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）按绝对路径打开可写后端的常规文件。`O_CREAT` 创建的新文件 MUST 记录调用进程身份为 owner 并采用调用方提供的 mode；打开/创建 MUST 在执行前经访问权限判定，并对非法路径、空间耗尽与权限拒绝确定性失败。

#### Scenario: O_CREAT 创建新文件

- **WHEN** 调用方以 `O_CREAT` 打开一个不存在的合法绝对路径且其目录可写、空间充足
- **THEN** BigOS MUST 在可写后端创建该文件、记录 owner 为调用进程 uid/gid、采用调用方 mode，并返回一个可写打开文件对象，offset 为 0

#### Scenario: O_TRUNC 截断已有文件

- **WHEN** 调用方以可写 flags 与 `O_TRUNC` 打开一个已存在且有写权限的文件
- **THEN** BigOS MUST 把文件长度截断为 0 并释放其多余数据块，offset 为 0

#### Scenario: 无空间创建失败

- **WHEN** 创建文件需要分配 inode 或数据块但位图已耗尽
- **THEN** BigOS MUST 返回确定性 `-ENOSPC`，MUST NOT 发布半成品 inode 或目录项

### Requirement: 文件写与 lseek

BigOS SHALL 提供从打开文件对象当前 offset 的有界写入与 `lseek` 定位。写 MUST 经块缓冲缓存写入并标脏、按成功写入字节推进 offset、对越界/无空间/IO 失败前不推进 offset，并校验所有 offset 算术溢出；`lseek` MUST 校验溢出，且对不可定位对象（如管道）返回确定性错误。

#### Scenario: write 推进 offset 且对读可见

- **WHEN** 调用方向一个可写打开文件写入合法用户缓冲
- **THEN** BigOS MUST 经缓存写入数据、按实际写入字节推进 offset，且后续对同一文件的读 MUST 看到写入内容

#### Scenario: write 失败不推进 offset

- **WHEN** 写入因无空间、IO 错误或用户缓冲校验失败而未成功交付字节
- **THEN** BigOS MUST 返回确定性错误，MUST NOT 推进 offset，MUST NOT 破坏已有数据

#### Scenario: lseek 定位与溢出校验

- **WHEN** 调用方对可定位文件以合法 whence 与 offset 调用 `lseek`
- **THEN** BigOS MUST 返回新的绝对 offset；当 offset 算术溢出或 whence 非法时 MUST 返回 `-EINVAL`

#### Scenario: 对管道 lseek 被拒绝

- **WHEN** 调用方对管道端文件对象调用 `lseek`
- **THEN** BigOS MUST 返回确定性 `-ESPIPE`

### Requirement: 目录项创建与删除

BigOS SHALL 在可写后端支持最小目录项创建（`mkdir`）与删除（`unlink`）。操作 MUST 在执行前经访问权限判定，对已存在/不存在、目录非空、对目录 `unlink`、空间耗尽与只读后端确定性失败，且 MUST NOT 引入完整 `readdir`/`getdents` 遍历或 `rename`。

#### Scenario: mkdir 创建目录

- **WHEN** 调用方对一个不存在的合法路径在可写、空间充足的父目录下 `mkdir`
- **THEN** BigOS MUST 创建目录项与目录 inode、记录 owner 与 mode，并返回成功

#### Scenario: mkdir 已存在被拒绝

- **WHEN** 调用方 `mkdir` 的路径已存在
- **THEN** BigOS MUST 返回确定性 `-EEXIST`，MUST NOT 修改文件系统状态

#### Scenario: unlink 删除常规文件

- **WHEN** 调用方对一个存在且有权限的常规文件 `unlink`
- **THEN** BigOS MUST 移除其目录项，并在无其它引用时释放其 inode 与数据块

#### Scenario: unlink 非法目标被拒绝

- **WHEN** 调用方 `unlink` 一个不存在的路径，或对目录调用 `unlink`
- **THEN** BigOS MUST 分别返回确定性 `-ENOENT` 或 `-EISDIR`，MUST NOT 修改文件系统状态

### Requirement: 写一致性与确定性失败语义

BigOS SHALL 以写回语义保证写后读回一致：写经缓存即对后续读可见，落盘发生在 `fsync`、缓存淘汰回写或全量同步时。可写后端的所有失败 MUST 以确定性错误返回，MUST NOT 在失败路径破坏已落盘元数据的一致性，MUST NOT panic（内核态 fault 除外）。

#### Scenario: write 经缓存即对读可见，fsync 落盘

- **WHEN** 调用方写入文件后未 `fsync` 即读回
- **THEN** BigOS MUST 返回写入后的内容（页缓存可见性）
- **AND** 调用 `fsync` 后内容 MUST 已落盘，且落盘后即便缓存淘汰再读仍一致

#### Scenario: 元数据更新失败不留半成品

- **WHEN** 创建/截断/删除在提交 inode、位图或目录项的过程中因空间或 IO 失败而中止
- **THEN** BigOS MUST 不发布半成品元数据、以确定性错误返回，并保持先前一致状态

### Requirement: 可写文件系统验证可复现

BigOS SHALL 通过默认关闭的运行时 smoke 与源码/行为断言验证可写文件系统。验证 MUST 记录工具链与模拟器可用性、串口 marker、跳过的用例与残余风险，且默认启动 marker 与既有 smoke 矩阵 MUST 保持不变。

#### Scenario: 可写 FS smoke 发射有界 marker

- **WHEN** 启用 `writable_fs_smoke`（`BIGOS_WRITABLE_FS_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「`O_CREAT` 建文件 + 写 + 读回一致」「`fsync` 落盘后淘汰再读一致」「owner/mode 权限拒绝」「只读后端写被 `EROFS` 拒绝」并发射 `BIGOS_WRITABLE_FS_PASSED`/`BIGOS_WRITABLE_FS_FAILED`
- **AND** 该开关 MUST 默认关闭；QEMU/Bochs、交叉工具链或磁盘镜像不可用时 MUST 记录为跳过验证而非声称通过
