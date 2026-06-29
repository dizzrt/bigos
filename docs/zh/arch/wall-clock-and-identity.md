# 墙钟时间与进程身份

本阶段在既有 PIT 单调 tick 与进程模型之上，新增一个最小墙钟与一个最小进程身份/权限层。两者都是有意做小、但语义正确的地基，供后续信号与可写文件系统阶段消费。

## 墙钟时间

### RTC 一次性基准 + 单调推进

墙钟在启动时建立一次基准，之后由既有单调 tick 推进，不做周期性 RTC 轮询。

- `bigos::time::init()` 在 PIT tick 可用之后（`enableIRQ()` 之后）、`proc::init()` 之前运行，使进程启动时间戳能观察到就绪的墙钟。
- 它经端口 `0x70`（index）/ `0x71`（data）通过 `driver::rtc` 驱动读一次 CMOS RTC，把 UTC 民用日期/时间换算为 Unix epoch 秒（`days_from_civil` 加时/分/秒），记为 `boot_unix_time`，并记录 `boot_tick = timer::ticks()`。
- `bigos::time::current_unix_time()` 返回 `boot_unix_time + (timer::ticks() - boot_tick) / TIMER_HZ`。查询路径不触硬件、不分配、不阻塞，在有界 kernel tick 模型下单调不减。`boot_unix_time()` 返回基准。

### RTC 读取正确性

`driver::rtc::read_time()` 在读取时间字段前对状态寄存器 A 的 update-in-progress（UIP）位做有界轮询，读状态寄存器 B 判断 BCD/二进制与 12/24 小时制，归一化字段（BCD 用 `(v & 0x0F) + (v >> 4) * 10`，处理 12 小时制 PM），并对各字段做范围校验（月 1-12、日 1-31、时 0-23、分/秒 0-59、年 0-99）。世纪固定按 2000 处理（不引入 ACPI FADT century 寄存器）。驱动不注册 IRQ8、不做周期轮询，是「读一次」只读 API。

### 确定性退化

若 UIP 轮询超限或任一字段越界，墙钟确定性退化：`boot_unix_time` 取固定基准（`RTC_INVALID_BASELINE = 0`，即 Unix epoch），仍记录 `boot_tick`，并向 COM1/VGA 发射固定 marker `BIGOS_RTC_INVALID`。绝不 panic、阻塞或分配；退化后当前时间查询仍在 0 基准之上单调可用。

### 已知限制

墙钟是「启动基准 + 单调推进」的近似。仅 UTC（无时区、夏令时、闰秒），无时钟同步（`settimeofday`/`adjtime`/NTP），长时间运行会与真实时间漂移，精度由 `TIMER_HZ` 决定。

## 进程身份与权限

### 身份四元组与启动时间戳

每个 `Process` 携带最小身份四元组 `uid`/`gid`/`euid`/`egid` 与 `start_unix_time` 墙钟创建时间戳（追加字段，既有布局不变）。

- init（PID 1）与非 fork ELF 创建默认 root（全 0）；各自在创建时记录 `start_unix_time = current_unix_time()`。
- `fork_current` 逐字段把父进程身份四元组复制进子进程，并以 fork 时墙钟标记子进程的 `start_unix_time`（子进程是新进程）。这不新增分配、不新增失败路径，也不改变 COW / 引用计数 / 回滚语义与「父返回子 PID、子返回 0」约定。
- `exec` 替换同一进程的镜像；不改变身份四元组，也不刷新 `start_unix_time`。

由于尚无 login 或 setuid，全系统以 root 运行；这些字段作为「可继承、可判定」的结构位，供信号与可写文件系统阶段使用。

### 纯判定原语

`include/bigos/cred.h` 导出纯的、无副作用的判定函数与 POSIX 数值布局权限位常量。本阶段不接线任何强制点。

- `bigos::cred::may_signal(actor, target)`：`actor->euid` 为 root 时放行，否则要求 `actor->euid` 匹配 `target->uid` 或 `target->euid`；空指针输入返回 false。这是未来 kill 判定的基础。
- `bigos::cred::permits(file_uid, file_gid, mode, req_uid, req_gid, access)`：root（`req_uid == 0`）始终放行；否则 `req_uid == file_uid` 用 owner 位、`req_gid == file_gid` 用 group 位、其余用 other 位；非法访问类型返回 false。供未来可写文件系统阶段复用。

## 验证

默认关闭的 `time_identity_smoke`（`xmake f --time_identity_smoke=y`，`BIGOS_TIME_IDENTITY_SMOKE`）发射 `BIGOS_TIME_IDENTITY_PASSED` / `BIGOS_TIME_IDENTITY_FAILED`。它覆盖墙钟随 tick 单调推进、非 fork 创建进程为 root 且 fork 逐字段继承身份、特权判定 root 放行 / 身份匹配放行 / 非匹配与空输入拒绝，以及文件访问判定。既有 smoke 矩阵与默认启动 marker 保持不变。

与其它进程 smoke 一样，该默认关闭 smoke 会在 init 运行前创建并拆除一个用户进程，因此 smoke 构建之后会观察到 `BIGOS_INIT_LOAD_FAILED map-failed`；这是 smoke 模式产物，并非正常启动行为。`tests/` 下的源码契约/行为断言测试固定新增 syscall 号位、身份字段与判定原语。
