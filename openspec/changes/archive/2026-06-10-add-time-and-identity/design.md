## Context

BigOS 当前的时间能力仅有 `bigos::timer` 提供的单调 tick（PIT IRQ0 推进，`tick_t g_ticks`），可回答「自启动以来过了多少 tick」，但无法回答「现在的墙钟时间是几点」，也没有把任何 RTC 来源接入内核。进程模型（`Process`，见 [proc.h](include/bigos/proc.h#L117-L168)）有 pid/parent_pid/state/exit_code 等字段，但没有任何身份概念（uid/gid）与时间戳，也没有「谁能对谁做特权操作」的判定。

阶段 16 已落地 `fork_current`（COW 复制地址空间、复制 fd 表），fork 子进程目前继承大部分父进程语义但没有身份字段可继承。阶段 17（信号）需要「谁能 kill 谁」的判定，阶段 18（可写文件系统）需要 owner/mode 与文件时间戳——两者都依赖本阶段提供的墙钟与身份原语。

约束：freestanding、单核、同步、无 libc；CMOS RTC 经端口 0x70（index）/0x71（data）访问，是阶段 18 之外唯一新引入的硬件访问点；syscall 走 `int 0x80`，寄存器 ABI 与现有号位固定，只能在末尾追加。

## Goals / Non-Goals

**Goals:**

- 提供一个最小但正确的墙钟：启动时读一次 CMOS RTC（UTC），转成 Unix epoch 秒作为基准，之后用现有单调 tick 推进，得到 `current_unix_time()`。
- 给 `Process` 加上最小身份四元组（uid/gid/euid/egid）与启动墙钟时间戳，并定义 init/ELF/fork/exec 各路径下的身份初始化与继承规则。
- 提供两个纯判定原语：进程间特权操作判定（未来 kill 的基础）与文件 owner/mode 访问判定（阶段 18 的基础），本阶段只提供判定逻辑、不接线任何强制点。
- 在 `int 0x80` 末尾新增只读身份/时间查询 syscall，让用户态可观察。
- 全部能力经默认关闭的 `time_identity_smoke` 与源码契约/行为断言验证，RTC 失败可确定性退化。

**Non-Goals:**

- 不做 setuid/setgid/seteuid 等身份变更、setuid 可执行位、login/认证、用户/组数据库。
- 不做时区/夏令时/闰秒、settimeofday/adjtime/NTP、持续 RTC 轮询、高精度计时器、完整 clockid。
- 不做文件时间戳写回、信号实际交付、可写文件系统强制点——这些是阶段 17/18 的工作；本阶段只交付它们消费的原语。
- 不改 `int 0x80` 寄存器 ABI、现有 syscall 号、IDT/向量、页表/CR3/地址布局、EOI 语义。

## Decisions

### 决策 1：墙钟 = RTC 一次性基准 + 单调 tick 推进，而非持续轮询 RTC

启动时读一次 RTC 得到 UTC 墙钟，换算为 `boot_unix_time`（Unix epoch 秒）；同时记录此刻的 `boot_tick = timer::ticks()`。之后 `current_unix_time() = boot_unix_time + (timer::ticks() - boot_tick) / TIMER_HZ`。

- 理由：单核早期内核不需要每秒读 RTC 的精度，且持续轮询 RTC 会引入新的周期性 IO 与同步问题；以单调 tick 推进保证墙钟单调、无需在热路径访问硬件、不在查询路径分配或阻塞。
- 备选：(a) 接 RTC 的 IRQ8 周期中断——引入新的 IRQ 路径与 EOI 处理，超出本阶段最小化目标，否决；(b) 每次查询都读 RTC——热路径硬件访问且 RTC 读取需要处理 update-in-progress，否决。
- 数据流：`boot -> rtc::read_time() (端口 0x70/0x71) -> 校验 + BCD/二进制归一 -> 民用日期 -> days_from_civil -> Unix 秒 = boot_unix_time` 并记录 `boot_tick`；查询路径只读 `timer::ticks()` 做算术，不触硬件。

### 决策 2：RTC 读取的正确性细节（update-in-progress、BCD、寄存器 B）

读 RTC 前轮询状态寄存器 A 的 UIP 位（bit 7）直到清零（带上限次数），再读秒/分/时/日/月/年；读状态寄存器 B 判断 BCD（bit 2=0 表示 BCD）与 12/24 小时制（bit 1）。BCD 值用 `(v & 0x0F) + (v >> 4) * 10` 归一。世纪固定按 2000 处理（年份字段 + 2000），并对各字段做范围校验。

- 理由：QEMU/Bochs 默认 RTC 多为 BCD，UIP 轮询避免读到半更新值；不引入 ACPI FADT century 寄存器（超范围），用固定世纪基准保持最小。
- 失败行为：UIP 轮询超过上限、寄存器值越界（月>12、日>31、时>23 等）-> `read_time` 返回失败；上层退化到 `boot_unix_time = 0`（epoch），发射诊断 marker（如 `BIGOS_RTC_INVALID`），绝不 panic、绝不阻塞、不分配。

### 决策 3：身份字段直接放进 `Process`，root=0，继承规则最小化

在 `Process` 增加 `uint32_t uid, gid, euid, egid;` 与 `int64_t start_unix_time;`。

- init（PID 1）：uid=gid=euid=egid=0（root），`start_unix_time = current_unix_time()`。
- `create_elf_user_process` / 非 fork 创建：默认 root（当前无 login，唯一身份来源是 init），`start_unix_time` 取创建时墙钟。
- `fork_current`：子进程逐字段继承父进程 uid/gid/euid/egid；`start_unix_time` 取 fork 时墙钟（子进程是新进程，时间戳为自身创建时刻）。
- `exec`：不改变身份字段（无 setuid 位），且不刷新 `start_unix_time`（见决策 7，exec 不是新进程）。
- 理由：在没有 login/setuid 的前提下，全系统只有 root 一个身份，字段存在的价值是为阶段 17/18 提供「可继承、可判定」的结构位，而非现在就区分多用户。备选「单独的 cred 结构体 + 引用计数」对当前单一身份是过度设计，否决。

### 决策 4：权限判定是纯函数，不接线强制点

提供两个纯判定函数（无副作用、对非法输入返回拒绝）：

- 进程特权操作：`bool may_signal(const Process *actor, const Process *target)` 语义——actor.euid==0（root）放行任意 target，否则要求 `actor.euid == target.uid`（或 target.euid），否则拒绝。命名以「特权操作判定」为准，kill 是首个未来消费者。
- 文件访问：定义 `mode` 权限位常量（owner/group/other 的 r/w/x，复用 POSIX 数值布局）与 `bool permits(uint32_t file_uid, uint32_t file_gid, uint32_t mode, uint32_t req_uid, uint32_t req_gid, Access access)`，root 全放行，否则按 owner/group/other 匹配对应权限位。
- 理由：本阶段没有信号、没有可写文件，没有任何地方需要强制；提前把判定逻辑做成纯函数可单测、可被阶段 17/18 直接复用，避免事后补权限。强制点接线明确留给消费阶段。

### 决策 5：syscall 只读追加，号位紧随现有末尾

现有最大号是 `SYS_FORK = 10`。新增：

```
SYS_GET_TIME = 11   // 返回 current_unix_time() (Unix 秒) 经 rax
SYS_GETPID   = 12   // 当前进程 pid
SYS_GETPPID  = 13   // 当前进程 parent_pid
SYS_GETUID   = 14   // 当前进程 uid
SYS_GETGID   = 15   // 当前进程 gid
```

- ABI：沿用现有寄存器约定（号 -> rax，返回值 -> rax），全部只读、无参数或仅平凡参数，不发 EOI（syscall 非外部 IRQ），不阻塞、不分配。
- 理由：只追加号位、不改动既有号与寄存器布局，满足非破坏性约束。备选「合并成一个带子命令的 syscall」会偏离现有「一号一义」风格，否决。

### 决策 6：`current_unix_time()` 返回有符号 `int64_t`

墙钟查询 API（`boot_unix_time()` / `current_unix_time()`）返回 `int64_t`。

- 理由：与既有有符号字段（如 `Process::exit_code`、`fault_reason`）风格一致，便于未来表示 epoch 之前的时间或错误哨兵值（如 -1），且 64 位有符号秒数远超内核可预期运行时长，无溢出风险。
- 备选：无符号 `uint64_t` —— 语义上墙钟当前不为负，但会丢失「错误/未知」可表达性并与既有有符号惯例不一致，否决。

### 决策 7：exec 不刷新 `start_unix_time`

`exec` 用新镜像替换地址空间时，保持 `start_unix_time` 与全部身份字段不变。

- 理由：exec 替换的是同一进程的镜像，不是新进程；`start_unix_time` 表示「进程创建时刻」，应在 fork/创建时确定并在 exec 间保持稳定，符合 POSIX 进程时间语义。
- 备选：exec 时刷新为当前墙钟 —— 会把「镜像替换时刻」与「进程创建时刻」混淆，且对未来 shell（`fork`+`exec`）观察子进程起始时间不利，否决。如阶段 19 有不同期望再单独评估。

### 决策 8：文件 `mode`/`Access` 常量本阶段放进公共头

文件权限位常量与 `Access` 枚举本阶段就放进公共头（如 `include/bigos/cred.h` 或合并进 proc 公共头），供阶段 18 可写文件系统直接引用。

- 理由：本阶段已实现 `permits(...)` 纯判定函数，其入参类型（mode 位、Access）天然是其公共契约的一部分；与判定函数同处公共头可避免阶段 18 再搬迁或重复定义。保持最小导出面：仅导出常量、枚举与判定函数签名，不导出内部实现细节。
- 备选：先放 proc 内部、阶段 18 再上提 —— 会导致一次无谓的搬迁与可能的重复定义，否决。

### 控制流总览

```
boot/init:
  mm/runtime init -> timer init (tick 已跑) -> time::init():
      rtc::read_time() ok? -> boot_unix_time = to_unix(rtc), boot_tick = ticks()
                            : boot_unix_time = 0 (+ BIGOS_RTC_INVALID marker), boot_tick = ticks()
  proc::init(): init 进程身份置 root, start_unix_time = current_unix_time()
  launch_init() ... fork_current(): 子进程继承父身份, start_unix_time = current_unix_time()

run-time 查询:
  user int 0x80 (SYS_GET_TIME/GETPID/...) -> dispatch -> 读 time/Process 字段 -> rax
  权限判定: may_signal / permits 为纯函数, 当前无调用强制点 (仅 smoke/未来阶段调用)
```

## Risks / Trade-offs

- [RTC 在不同模拟器/真机的 BCD/12h/世纪差异导致基准时间错误] → 读寄存器 B 判断 BCD 与 12/24h，世纪固定 2000，字段范围校验；任何异常退化到 epoch 0 并发 marker，墙钟仍单调可用。
- [墙钟随 tick 推进，长时间运行会与真实时间漂移] → 本阶段非目标是时钟同步；明确记录这是「启动基准 + 单调推进」近似，精度由 TIMER_HZ 决定，阶段后续可加重同步。
- [身份字段当前全是 root，等于没有真正的多用户隔离] → 明确这是为阶段 17/18 预留的结构位；判定函数已正确实现 root/非 root 分支，待 login/setuid 落地即生效，无需重构。
- [新增 RTC 端口 IO 引入硬件访问] → 仅启动时读一次、不在 IRQ 上下文、不在查询热路径；与 PIT/i8259 端口访问风格一致，限定在 RTC 驱动 TU 内。
- [syscall 号追加若与未来阶段冲突] → 在 syscall.h 集中定义并由源码契约测试固定号位；后续阶段同样只在末尾追加。

## Migration Plan

- 纯增量、无运行时迁移：新增 RTC 驱动、time 模块、Process 字段、判定函数与只读 syscall，均默认参与正常启动但不改变既有行为（身份默认 root 等价于此前隐含语义）。
- 回滚：移除 `time_identity_smoke` 与新增 syscall 分支即可回到原状；Process 新增字段为追加，不影响既有布局假设。
- 验证开关 `time_identity_smoke` 默认关闭，不影响默认启动 marker。

## Open Questions

- 无。原先的三个待定项已收敛为决策 6（`current_unix_time()` 返回 `int64_t`）、决策 7（exec 不刷新 `start_unix_time`）、决策 8（文件 `mode`/`Access` 常量本阶段放进公共头）。如阶段 19 shell 对子进程起始时间有不同期望，可再单独评估决策 7。
