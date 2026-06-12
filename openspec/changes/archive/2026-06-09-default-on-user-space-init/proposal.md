## Why

当前 normal boot 永远不会进入 ring3：唯一调用 `run_user_process` 的位置都藏在默认关闭的
`user_program_smoke` / `user_elf_smoke` 开关之后（见 [kernel.cc](kernel/core/kernel.cc#L337-L345)）。这意味着进程 / `exec` / VFS / ELF 加载这些核心能力只在 smoke
构建里被偶发验证，而非默认路径上被持续运行。Stage 14.5 是后续所有 POSIX 工作（demand
paging、fork/COW、信号、可写 FS、userland）的前置起跑线：必须先让 normal boot 真正运行一个
用户进程，并借此启动「行为断言测试」轨道。

## What Changes

- 在 [proc::init()](kernel/core/kernel.cc#L323) 与 [sched::start()](kernel/core/kernel.cc#L350) 之间，新增一个**默认开启、无
  `#ifdef` 守卫**的 `launch_init()`：复用现有 `vfs::init` -> 读取
  `/boot/user/init.elf` -> `create_elf_user_process` -> `run_user_process` 路径，使其从
  smoke `#ifdef` 中解放出来，成为 normal boot 的固定步骤。
- 定义 PID-1 / init 语义：`/boot/user/init.elf` 缺失、过大或非法 ELF 时，走确定性降级，
  带 `BIGOS_INIT_*` marker 进入统一 panic 路径；定义 init 正常退出 / 被 reap 后内核的
  确定性行为约定。
- 在默认构建（不加任何 smoke 开关）中产出 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT`
  serial marker，并把该默认 case 纳入 Stage 9 runtime smoke 矩阵。
- 默认构建打包 `/boot/user/init.elf` 用户态二进制（此前仅 `user_elf_smoke` 构建），使
  normal boot 始终具备可运行的 init 镜像。
- 保留 `user_program_smoke` / `user_elf_smoke` 作为额外验证开关，**不删除**已有 smoke
  矩阵与其用例。
- 启动「行为断言测试」轨道：新增基于 serial marker + 用户态二进制输出的行为断言用例，作为
  Stage 9 矩阵的演进起点（持续推进，非一次性完成）。

## Capabilities

### New Capabilities
- `user-space-init`: normal boot 默认进入 ring3 的 init 启动契约——`launch_init`
  在 `proc::init()` 与 `sched::start()` 之间无 `#ifdef` 地加载并运行
  `/boot/user/init.elf`，定义 init 缺失 / 非法的确定性降级、init 退出 / 被 reap 后的内核
  行为，以及 `BIGOS_INIT_*` marker 契约。

### Modified Capabilities
- `runtime-smoke-validation`: Stage 9 矩阵新增一个**默认构建**（无 smoke 开关）case，
  断言 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT`；并引入行为断言（serial marker + 用户态
  二进制输出）作为矩阵演进方向。

## Impact

- 受影响子系统：kernel 入口（`kernel/core/kernel.cc`）、proc（init 启动 / 退出语义，
  `kernel/core/proc/`）、构建系统（`xmake.lua` 默认打包 `/boot/user/init.elf`）、boot
  打包流程（`tools/boot_debug.py` 与磁盘镜像安装）、Stage 9 smoke 矩阵与验证脚本。
- 受影响 API / 代码：新增内核内部 `launch_init` 入口；复用 `vfs::init`、
  `bigos::proc::create_elf_user_process`、`bigos::proc::run_user_process`、
  `USER_ELF_SMOKE_PATH` / `USER_ELF_MAX_FILE_BYTES` 等既有 proc/VFS 接口。
- 假设：架构仍为 x86_64-only，磁盘为 Legacy BIOS/MBR/exFAT 只读路径，ELF64 `ET_EXEC`
  bounded 加载，CR3 切换与 `iretq` ring3 入口保持现状；不改动 boot 地址、linker 地址、
  IDT/syscall 向量、页表自映射地址或磁盘偏移。
- 非目标（本 change 明确不做）：demand paging、fork/COW、broad mmap、信号、可写 FS、
  用户态 libc、多进程 shell、SMP、UEFI；不引入新 syscall；不改动 ELF 加载器的 bounded
  限制；不删除既有 smoke 开关。
