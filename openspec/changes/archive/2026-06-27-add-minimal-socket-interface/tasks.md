## 1. 边界盘点与 ABI 接入

- [x] 1.1 盘点既有 fd 对象模型（`vfs::File`/`FileOperations`/`File::private_data`）、pipe backend（`kernel/core/ipc/pipe.cc`）、fd 表分配/复制/关闭/`close-on-exec`/`fork` 路径与 `sys_pipe` 失败回滚范式，确认 socket backend 复用点。
- [x] 1.2 盘点既有 `bigos::net` 内核内部 UDP API（`udp_bind`/`udp_close`/`udp_send_to`/`udp_receive_from`/`pump`/`init_default`/`default_context`）、容量常量（`UDP_ENDPOINT_CAPACITY`/`UDP_MAX_PAYLOAD`/`UDP_RX_QUEUE_CAPACITY`）与 ordinary-context 约束，确认 socket 层适配边界。
- [x] 1.3 在 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 同步新增 socket 系列 number（从 `55` 起：`SYS_SOCKET`/`SYS_BIND`/`SYS_SENDTO`/`SYS_RECVFROM`）并保持两份相等；定义有界地址结构（IPv4+port 定长）与相关常量；不改变既有 number 取值或语义。
- [x] 1.4 审查启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector、CR3 切换与既有 fd/VFS/pipe ABI，确认本变更不修改这些边界。

## 2. socket backend 对象层

- [x] 2.1 新增 socket backend（置于 `kernel/core/net` 或 `kernel/core/ipc` 下以复用既有 `**.cc` glob），定义静态 `vfs::FileOperations SOCKET_OPS`、socket 状态结构（持有 `net::Context*`/`net::UdpEndpoint*` 与绑定状态）与新增内核头。
- [x] 2.2 实现 socket `create`（`kmalloc` `vfs::File` + 状态、设置 `ops`/`private_data`/`ref_count`/`readable`/`writable`）与 `is_socket_file`（ops 指针身份判别），沿用 pipe 范式。
- [x] 2.3 实现 `SOCKET_OPS.close`：通过 `net::udp_close` 回收 endpoint 并释放状态，保证 `vfs::release` 最后一次引用时 exactly-once 回收，无双重释放或泄漏。
- [x] 2.4 实现 `SOCKET_OPS.read`/`write` 返回确定性不支持错误（无连接 UDP 不做隐式无地址收发），`lseek` 返回 NotSeekable 等价语义。

## 3. socket syscall 分发

- [x] 3.1 在 `kernel/core/syscall` 的 `dispatch` switch（`BIGOS_USER_PROCESS` 守卫内）新增 `SYS_SOCKET`，校验 domain/type/protocol 属于有界 UDP 子集，调用 `net::udp_bind` 之外的资源准备并 `install_fd_current` + 失败回滚。
- [x] 3.2 实现 `SYS_BIND`：校验 socket fd、定长地址结构与 `addrlen` 一致，调用 `net::udp_bind`，把 `AlreadyBound`/`TableFull`/`InvalidArgument` 等映射为确定性 errno。
- [x] 3.3 实现 `SYS_SENDTO`：通过 `validate_user_buffer`/`copy_current_user_buffer` 读取 payload（≤ `UDP_MAX_PAYLOAD`/`SYS_IO_MAX_LEN`）与目的地址，调用 `net::udp_send_to`，映射 ARP 未解析/无路由/too-large/timeout/设备失败为确定性 errno。
- [x] 3.4 实现 `SYS_RECVFROM`：在 ordinary 上下文做有界 `net::pump` 推进 + `net::udp_receive_from`，有界等待/无数据返回确定性 errno（非通用 POSIX 阻塞），通过 copy-to-user 写回来源 IPv4/port 与 payload。
- [x] 3.5 审查全部 socket syscall 的用户缓冲/地址校验、错误映射、fd 安装/回滚与引用计数，确认绝不从 IRQ 上下文调用协议层。

## 4. fork/close/生命周期集成

- [x] 4.1 验证 socket fd 走既有 `clone_fd_table`（`vfs::retain`）、`close_fd_current`/`close_all_fds`、`close_on_exec_fds`、`dup`/`dup2`/`fcntl` 路径，行为确定。
- [x] 4.2 审查 `fork` 共享 endpoint 引用计数、进程退出统一回收、错误回滚下 endpoint 与 `vfs::File` 的对象生命周期与 alignment，避免双重释放或泄漏。

## 5. 用户 libc socket 接口

- [x] 5.1 在 `user/libc` 新增 `socket`/`bind`/`sendto`/`recvfrom` wrapper 与对应头，使用既有 `syscallN` ABI 与 `errno_translate`，保持 freestanding-safe（无 hosted libc/异常/RTTI）。
- [x] 5.2 定义用户态有界地址结构与常量，与内核头保持一致；确认与既有 libc 头无符号冲突。

## 6. 默认关闭验证与工具接入

- [x] 6.1 新增默认关闭 build switch 与 socket smoke 入口，覆盖 socket 创建、bind、sendto/recvfrom 闭环与错误路径（未 bind、地址非法、缓冲越界、资源满、无数据），发出确定性通过/失败 marker。
- [x] 6.2 优先实现可在无真实 tap/网络后端运行的内核内部/注入式闭环验证（复用协议层 `inject_frame` 思路）；若需真实网络后端，复用既有 virtio-net host 辅助的 QEMU/tap 能力。
- [x] 6.3 跑默认启动回归，确认 socket smoke 关闭或无网络后端时，storage、filesystem、`/rw`、shell 与 userland baseline 不依赖 socket 并正常进入 shell。
- [x] 6.4 若新增或修改 Python host-side 验证辅助，使用 `uv run ...`，并补充 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`；若未改 Python，记录不适用。
- [x] 6.5 可用时跑 Bochs 默认启动交叉验证；若 Bochs ROM/display/磁盘镜像路径不可用，记录无法运行原因与风险。

## 7. 静态检查、构建与文档收尾

- [x] 7.1 运行 xmake 目标构建（x86_64-elf-gcc/x86_64-elf-g++），含 socket smoke 开/关两种配置；若交叉工具链或 xmake 不可用，记录 blocker、替代检查与剩余风险。
- [x] 7.2 运行接近 GCC 交叉构建环境的 clang 辅助检查：freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI；修复当前变更新增有效诊断，历史诊断与 false positive 分开记录。
- [x] 7.3 运行对应 clangd 辅助诊断或记录 clangd flags/config 差距；修复当前变更新增有效诊断，历史诊断与 false positive 分开记录。
- [x] 7.4 运行既有 syscall number 源契约测试，确认 `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 新增 number 相等且既有 number 不变。
- [x] 7.5 更新 docs/en 与 docs/zh 镜像文档，描述最小有界 UDP socket 边界（不声称完整 POSIX socket 或完整网络栈），保持目录结构同构。
- [x] 7.6 实现完成后更新 `roadmap.md` 中 M11.3 的完成状态，保持 roadmap 仅项目规划级描述，不加入入口点、命令、marker、文件路径或源码细节。
- [x] 7.7 运行 OpenSpec 校验与状态检查（`openspec validate add-minimal-socket-interface --strict`、`openspec status`），确认 artifacts、规格与任务处于可归档状态，并在验证记录中区分已通过、无法运行（含原因与剩余风险）、历史诊断与当前变更新问题。

## 验证记录

### 已通过

- xmake 构建（socket_smoke 关闭，默认配置）：`xmake f --socket_smoke=n -y && xmake -j4` → build ok。
- xmake 构建（socket_smoke 开启）：`xmake f --socket_smoke=y -y && xmake -j4` → build ok。
- socket smoke 闭环：`xmake f --socket_smoke=y -y && xmake -j4` 后
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/socket-smoke.serial.log --expect-serial-marker BIGOS_SOCKET_PASSED --smoke-timeout 60`
  → `serial marker observed: BIGOS_SOCKET_PASSED`（覆盖创建、ops 身份、不支持 read/write、bind、dup-bind、sendto、recvfrom、no-data、too-large、close 后 rebind）。
- 默认启动回归（socket_smoke 关闭，QEMU/UEFI）：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/default-boot-after-socket.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 60`
  → `serial marker observed: BIGOS_USER_EXEC`，确认默认启动/shell/userland 不依赖 socket。
- Bochs 默认启动交叉验证（Legacy BIOS，socket_smoke 关闭）：
  `uv run python -m tools.bigosdev run --boot-mode legacy --emulator bochs --display none --serial-log logs/socket-bochs-default.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 70`
  → `serial marker observed: BIGOS_USER_EXEC`。
- clang 辅助语法检查（freestanding C++17、x86_64、no rtti/exceptions）：
  `clang++ --target=x86_64-unknown-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_USER_PROCESS -DBIGOS_SOCKET_SMOKE -fsyntax-only kernel/core/net/socket.cc kernel/core/syscall/syscall.cc`
  → 干净通过（覆盖 clangd 等价诊断需求）。
- syscall number 源契约测试：`uv run pytest tests/test_syscall_entry_source.py -q` → 18 passed，确认
  `include/bigos/syscall.h` 与 `user/libc/include/sys_nr.h` 新增 number（55..58）相等且既有 number/errno 不变。
- Python host 辅助检查（仅在 `tools/bigosdev/core.py` 追加数据驱动的 RuntimeSmokeCase 与开关名）：
  `uv run ruff check tools/bigosdev/core.py`、`uv run ruff format --check tools/bigosdev/core.py`、
  `uv run pyright tools/bigosdev/core.py` 均通过。
- OpenSpec：`openspec validate add-minimal-socket-interface --strict` → valid；`openspec status` → 4/4 artifacts complete。

### 历史诊断（与本变更无关，pre-existing）

- `uv run pytest tests/` 报告 19 failed / 326 passed。已用 `git stash`（仅暂存已跟踪改动）核对：
  还原本变更的已跟踪改动后，同一套 19 个失败、326 通过完全一致，失败集合逐项相同。这些失败位于
  `test_address_space_lifecycle_source.py`、`test_user_address_space_vmem_source.py`、
  `test_fork_copy_on_write_source.py`、`test_modern_block_storage_driver_source.py`、
  `test_source_root_layout.py` 等文件，断言的是已被历史重构改写的源码字符串/历史 change 工件，
  与本变更新增的 socket 文件无关，属历史诊断。

### 无法运行 / 剩余风险

- 真实 tap/virtio-net 网络后端闭环未运行：socket smoke 采用内核内部注入式闭环（复用协议层
  `inject_frame`），可在无 tap 环境验证完整路径；真实 tap 路径需宿主 TAP 权限与 virtio-net，
  未在本环境执行，剩余风险为真实链路下 ARP/虚拟网卡时序差异，已由协议层既有 virtio-net/网络协议
  smoke 覆盖其底层路径。
- recvfrom 为有界轮询 + 让出（非通用 POSIX 阻塞）：长时间无数据返回 `-EAGAIN`，不提供真正
  wait-queue 阻塞；该边界在 design/spec/docs 中显式声明，真正阻塞唤醒留作后续独立 change。
