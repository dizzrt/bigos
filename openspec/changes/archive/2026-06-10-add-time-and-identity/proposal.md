## Why

当前内核只有一个由 PIT IRQ0 驱动的 `bigos::timer` 单调 tick，没有任何「墙钟时间」来源：无法回答「现在是几点」，进程也没有创建/启动时间戳。同时进程模型完全没有身份概念——所有进程隐含等价、没有 uid/gid，也没有「谁能对谁做什么」的权限判定。路线图阶段 16.5（时间与身份）要求在 fork/COW（阶段 16，已完成）之后、信号（阶段 17）与可写文件系统（阶段 18）之前补齐这两类原语，因为后两者都直接依赖它们：信号需要「谁能 kill 谁」的身份/权限判定，可写文件系统需要 owner/mode 与文件时间戳。趁现在语义面仍小（单核、同步、无信号）时把墙钟与身份立成最小但正确的地基，可避免日后在信号与 FS 上事后补权限。

## What Changes

- 新增墙钟/RTC 时间来源：在保留现有 `bigos::timer` 单调 tick 的前提下，新增一个从 CMOS RTC 读取一次基准时间（年/月/日/时/分/秒，UTC）、再以单调 tick 推进的「墙钟」。提供 `bigos::time` 下的只读查询 API（如 `boot_unix_time()` 基准与 `current_unix_time()` 当前墙钟秒数），把启动时刻的 RTC 读数转换为 Unix epoch 秒。
- 新增进程身份字段：在 `Process` 结构中加入最小身份四元组 `uid`/`gid`/`euid`/`egid`，并加入进程时间戳（如 `start_unix_time`，基于上面的墙钟）。`init`（PID 1）以 root 身份（uid=gid=0）启动；`fork` 时子进程继承父进程的全部身份字段；`exec` 不改变身份（暂不实现 setuid 位）。
- 新增最小权限模型原语：提供「调用方进程能否对目标进程执行特权操作（如未来的 kill）」的判定函数（root 即 uid==0 可操作任意目标，否则要求 uid 匹配），以及供阶段 18 文件 owner/mode 复用的权限位常量与「(uid,gid,mode,请求访问) -> 允许/拒绝」纯判定函数。本阶段只提供原语与判定逻辑，不接线任何强制点（没有信号、没有可写文件）。
- 新增最小身份查询 syscall：在 `int 0x80` ABI 末尾新增固定号 `SYS_GET_TIME`（返回当前墙钟 Unix 秒）、`SYS_GETPID`/`SYS_GETPPID`、`SYS_GETUID`/`SYS_GETGID`，让用户态可观察时间与身份。寄存器 ABI、现有 syscall 号、向量布局、「syscall 不发 EOI」均不变，仅在末尾追加新号。
- 定义确定性失败语义：RTC 读数无效或越界（如 BCD/二进制模式判断失败、字段超范围）-> 退化为固定基准（如 epoch 0）并发射一个诊断 marker，绝不 panic、绝不阻塞、绝不在墙钟路径中分配内存；权限判定函数对非法输入返回「拒绝」而非崩溃。
- 新增默认关闭的验证开关 `time_identity_smoke`（`BIGOS_TIME_IDENTITY_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_TIME_IDENTITY_PASSED` / `BIGOS_TIME_IDENTITY_FAILED`），覆盖「墙钟在 RTC 基准之上随 tick 单调推进」「init 身份为 root 且 fork 子进程继承身份」「权限判定 root 放行/非匹配拒绝」「RTC 无效时确定性退化」等路径；保留现有 smoke 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` 寄存器 ABI 约定（仅在末尾追加新 syscall 号）、IDT/向量布局、页表自映射地址、CR3 切换约定、higher-half/direct-map/`KVMEM_BASE` 布局或用户低半区布局；`#PF` 与外部 IRQ EOI 语义不变；不引入 SMP/锁、信号交付、可写文件系统、文件时间戳写回或用户态 libc。

## Capabilities

### New Capabilities
- `wall-clock-time`: 墙钟时间能力——一次性 CMOS RTC 基准读取（UTC、BCD/二进制模式判断、字段校验）、转换为 Unix epoch 秒、以现有单调 tick 推进的当前墙钟查询 API，以及 RTC 无效时的确定性退化与默认关闭验证开关。
- `process-identity-permissions`: 进程身份与最小权限能力——`Process` 的 uid/gid/euid/egid 与进程墙钟时间戳、init root 身份与 fork 身份继承、「能否对目标进程执行特权操作」与「(uid,gid,mode,访问) -> 允许/拒绝」的纯判定原语（供阶段 17/18 消费），以及非法输入一律拒绝的失败语义。

### Modified Capabilities
- `process-lifecycle`: `Process` 新增 uid/gid/euid/egid 与进程启动墙钟时间戳字段；进程创建（init/ELF/fork）按规则初始化或继承这些身份字段，fork 子进程继承父身份，exec 不改变身份。
- `fork-copy-on-write`: `fork` 复制语义新增「身份字段（uid/gid/euid/egid）由父进程继承到子进程」的要求；不改变既有 COW 地址空间复制、引用计数与失败回滚语义。
- `syscall-entry`: `int 0x80` ABI 在末尾新增 `SYS_GET_TIME`/`SYS_GETPID`/`SYS_GETPPID`/`SYS_GETUID`/`SYS_GETGID` 只读身份/时间查询号；返回值经 rax 回写，其余 syscall 号、寄存器约定与「syscall 不发 EOI」不变。

## Impact

- 受影响子系统：`kernel/drivers`（新增 CMOS RTC 一次性读取驱动）、`kernel/core/timer` 或新增 `kernel/core/time`（墙钟基准 + 当前墙钟查询）、`kernel/core/proc`（`Process` 身份字段、init/fork 身份初始化与继承、权限判定原语）、`kernel/core/syscall`（新增只读身份/时间 syscall 分发）。
- 受影响代码：[timer.h](include/bigos/timer.h) 或新增 `include/bigos/time.h`（墙钟 API）、新增 RTC 驱动头/源（`include/drivers/rtc/*`、`kernel/drivers/rtc/*`）、[proc.h](include/bigos/proc.h)（`Process` 身份/时间字段与权限判定声明）、[proc.cc](kernel/core/proc/proc.cc)（身份初始化/继承、权限判定实现）、[syscall.h](include/bigos/syscall.h) 与 [syscall.cc](kernel/core/syscall/syscall.cc)（新增 syscall 号与分支）。
- 构建/验证：`xmake.lua` 新增 `time_identity_smoke` 开关；QEMU headless serial-marker smoke 与源码契约/行为断言测试（沿用阶段 14.5 启动的行为断言测试轨道）。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 不变；CMOS RTC 经端口 0x70/0x71 访问，QEMU/Bochs 提供可读 RTC；RTC 仅在启动时读取一次，之后墙钟靠单调 tick 推进（不做持续 RTC 轮询、不做时钟同步/校准、不处理时区/夏令时，全部按 UTC）；`kmalloc`/`free` 在进程创建（非 IRQ）上下文可用；阶段 16 的 fork/COW 与可增长进程/fd 表已就位；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：完整 POSIX 用户/组数据库（`/etc/passwd`、组列表、supplementary groups）、`setuid`/`setgid`/`seteuid` 等身份变更 syscall、setuid 可执行位、login/认证、时区/夏令时/闰秒、`settimeofday`/`adjtime`/NTP 时钟同步、持续 RTC 轮询、高精度计时器、`clock_gettime` 全部 clockid、文件时间戳写回、信号实际交付与可写文件系统的强制点接线。这些留给阶段 17/18/19 与后续工作。
