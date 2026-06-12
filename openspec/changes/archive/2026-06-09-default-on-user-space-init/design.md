## Context

当前 normal boot 在 [kernel.cc](kernel/core/kernel.cc#L287-L351) 中的尾部顺序为：
`init_mem` -> `init_tty` -> `initIRQ` -> `enableIRQ` -> `proc::init()` -> `sched::start()`。
唯一进入 ring3 的代码是 `user_elf_smoke_entry`（[kernel.cc](kernel/core/kernel.cc#L225-L285)）
与 `proc::user_program_smoke_entry`，二者都被 `#ifdef BIGOS_USER_ELF_SMOKE` /
`BIGOS_USER_PROGRAM_SMOKE` 包裹，默认不编译，也不打包 `/boot/user/init.elf`。

`/boot/user/init.elf` 的构建同样受 smoke 守卫：[xmake.lua](xmake.lua#L201-L227) 中的
`user-init-elf` target 仅在 `has_config("user_elf_smoke")` 时 `set_default(true)`。

已有可复用的构件：`vfs::init`、`bigos::proc::create_elf_user_process`、
`bigos::proc::run_user_process`、常量 `USER_ELF_SMOKE_PATH = "/boot/user/init.elf"`
与 `USER_ELF_MAX_FILE_BYTES = 64*1024`、`user_elf_load_error_name`，以及统一 panic
路径与现成的 zombie/reaper teardown。本 change 的本质是把 `user_elf_smoke_entry`
的核心逻辑「去 smoke 化」为默认路径，并补齐 init 缺失/退出语义与默认 marker。

## Goals / Non-Goals

**Goals:**
- 在 `proc::init()` 与 `sched::start()` 之间新增默认开启、无 `#ifdef` 守卫的
  `launch_init`，使 normal boot 默认进入 ring3。
- 复用现有 VFS/ELF 加载链路，不新增 syscall、不放宽 ELF bounded 限制。
- 默认构建打包 `/boot/user/init.elf`。
- 定义并实现 init 缺失/非法 -> `BIGOS_INIT_*` panic 的确定性降级，以及 init
  退出/被 reap 后的确定性内核行为。
- 默认构建发出 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT` marker，并纳入 Stage 9 矩阵默认 case。

**Non-Goals:**
- demand paging、fork/COW、broad mmap、信号、可写 FS、用户态 libc、多进程 shell、SMP、UEFI。
- 不删除 `user_program_smoke` / `user_elf_smoke` 开关与其矩阵用例。
- 不改动 boot 地址、linker 地址、IDT/syscall 向量、页表自映射、磁盘偏移、CR3 切换规则。
- 不实现完整 PID-1 重启/收养语义；仅做雏形（缺失即 panic、退出即确定性收尾）。

## Decisions

### 决策 1：`launch_init` 作为内核内部入口，置于 `proc::init()` 之后
在 `kernel()` 尾部、`sched::start()` 之前调用 `bigos::proc::launch_init()`（或
kernel.cc 内的等价非 `#ifdef` 函数）。理由：此时 mm/IRQ/timer/TTY/proc 均已就绪，
与现有 `user_elf_smoke_entry` 的运行前提一致。
- 备选：放在独立 kernel 线程中（如 smoke 当前做法）。权衡：smoke 用线程是为了与其他
  smoke 并存；默认 init 是唯一首进程，直接在主流程调用更简单、语义更清晰。若实现上需要
  调度上下文，可在 `sched::start()` 后由 idle/首线程驱动，但仍保持无 `#ifdef`。

### 决策 2：复用 `create_elf_user_process` + `run_user_process`，不抽象新接口
`launch_init` 内部沿用 `vfs::init` -> `open_absolute(USER_ELF_SMOKE_PATH)` ->
读入 bounded 缓冲 -> `create_elf_user_process` -> `run_user_process`，与
[user_elf_smoke_entry](kernel/core/kernel.cc#L233-L283) 一致。
- 备选：把 smoke entry 直接改名复用。权衡：smoke entry 发的是 `BIGOS_USER_ELF_*`
  marker 且需保留给 smoke 开关；因此新增 `launch_init` 并发 `BIGOS_INIT_*`，两者共享底层调用。

### 决策 3：引入中性常量 `INIT_ELF_PATH`，与 `USER_ELF_SMOKE_PATH` 同值
默认 init 与 `user_elf_smoke` 共用 `/boot/user/init.elf`。**定稿**：引入语义中性的
`INIT_ELF_PATH` 常量指向 `/boot/user/init.elf`，由 `launch_init` 使用；现有
`USER_ELF_SMOKE_PATH` 保持不变（可定义为指向同一字符串）以避免改动 smoke 路径。
理由：default init 已非 smoke-only，常量名应反映其默认路径语义，避免后续读者误以为
init 依赖 smoke。
- 备选：直接复用 `USER_ELF_SMOKE_PATH`。权衡：零新增符号但语义误导，默认路径仍挂着
  `_SMOKE_` 名称。
- 备选：为 init 使用独立路径（如 `/sbin/init`）。权衡：会牵动磁盘打包与 VFS 路径，
  超出本阶段范围；沿用既有路径成本最低。

### 决策 4：构建默认打包 init.elf
将 [user-init-elf](xmake.lua#L201-L227) target 在默认构建中 `set_default(true)`
（不再以 `user_elf_smoke` 为条件），并确保磁盘镜像安装流程默认包含
`/boot/user/init.elf`。`user_elf_smoke` 继续复用同一产物。

### 决策 5：init 缺失/非法 -> 统一 panic；正常退出 -> 发 `BIGOS_INIT_EXIT` 后进入 idle
- 缺失/超限/非法 ELF：发 `BIGOS_INIT_*`（含原因）后走统一 panic 路径（PID-1 语义雏形）。
- 正常退出：**定稿**为发出 `BIGOS_INIT_EXIT` 后进入现有 idle 调度（halt），而非 panic。
  理由：行为最小、可确定性观察 `ENTER`/`EXIT` 转换，且与既有 idle-thread 拥有 halt 的
  设计一致；本阶段不实现 PID-1 重启/收养。
- 备选：init 退出即 panic（视 init 退出为致命）。权衡：语义更严格，但会把「init 正常
  跑完并 exit」也判为失败，不利于行为断言观察正常 ENTER/EXIT，留作后续阶段按需收紧。

### 决策 6：本 change 只断言内核 `BIGOS_INIT_*` marker，stdout 行为断言留待后续
新增默认构建（无 smoke 开关）的 Stage 9 矩阵 case，**定稿**为本 change 仅以
`BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT` 内核串口 marker 为通过判据，启动「行为断言
测试」轨道；init 二进制自身的 stdout 输出断言留待后续阶段（待用户态有稳定写出路径后）
再引入。理由：当前 init.elf 是最小汇编程序，先用内核 marker 建立行为断言基线，避免本
change 绑定尚不稳定的用户态输出约定；该轨道逐步替代源码字符串契约断言（如
`test_user_elf_program_loader_source.py` 风格）。
- 备选：本 change 即引入 init stdout 输出断言。权衡：更接近「真正用户程序行为」，但需先
  约定用户态写出/串口路径，扩大范围且耦合未定接口。

## Risks / Trade-offs

- [默认进入 ring3 增加 normal boot 复杂度，可能影响其他 smoke 的稳定性]
  → 保留所有既有 smoke 开关与 marker 不变，新增 marker 用独立 `BIGOS_INIT_*`
  前缀，避免与 `BIGOS_USER_*` 混淆。
- [init.elf 缺失/损坏会导致默认 boot panic（此前默认 boot 不会因此 panic）]
  → 这是有意的 PID-1 语义雏形；用确定性 `BIGOS_INIT_*` marker + 统一 panic 暴露，
  并在文档/矩阵中明确该新失败模式。
- [默认打包 init.elf 改动磁盘镜像内容，可能影响镜像大小/安装流程]
  → 复用既有 64KiB bounded 上限与既有打包路径；仅把条件从 smoke 改为默认。
- [行为断言轨道刚启动，可能与既有源码契约测试并存产生冗余]
  → 本 change 只新增默认 init 行为断言 case，不一次性重写所有源码契约测试；视为持续演进起点。

## Migration Plan

1. 实现 `launch_init` 并接入 `kernel()`（无 `#ifdef`）。
2. 修改 `xmake.lua` 使 `user-init-elf` 默认构建，磁盘镜像默认含 `/boot/user/init.elf`。
3. 实现 `BIGOS_INIT_*` marker 与缺失/退出语义。
4. 新增 Stage 9 默认 init 矩阵 case 与行为断言验证。
5. 回滚策略：若默认 init 引入不稳定，可临时将 `launch_init` 调用与 init.elf 默认打包
   回退为开关控制（仅作应急，非目标状态），既有 smoke 开关始终可用作旁路验证。

## Open Questions

无未决项：原三项（init 退出行为、`INIT_ELF_PATH` 命名、行为断言范围）已分别收敛进
决策 5、决策 3、决策 6。
