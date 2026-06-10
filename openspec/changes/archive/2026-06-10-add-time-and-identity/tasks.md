## 1. CMOS RTC 一次性读取驱动

- [x] 1.1 在 `include/drivers/rtc/` 与 `src/drivers/rtc/` 新增 CMOS RTC 驱动：经端口 0x70（index）/0x71（data）读取秒/分/时/日/月/年，读取前轮询状态寄存器 A 的 UIP 位（带有界上限），读状态寄存器 B 判断 BCD 与 12/24 小时制并归一化字段。
- [x] 1.2 实现字段范围校验（月 1-12、日 1-31、时 0-23、分/秒 0-59 等），校验失败或 UIP 轮询超限时返回失败结果，世纪固定按 2000 处理。
- [x] 1.3 RTC 驱动仅暴露「读一次」API，确保不注册 IRQ8、不做周期轮询；限定端口访问在该 TU 内，保持与 PIT/i8259 一致的低层 IO 风格。
- [x] 1.4 接口与硬件访问安全审查：端口 IO 顺序、UIP 轮询有界、无 IRQ 上下文调用、可见失败而非阻塞。

## 2. 墙钟时间模块

- [x] 2.1 新增 `include/bigos/time.h` 与 `src/kernel/time/`（或并入 timer 子系统）：实现 `time::init()` 在 tick 可用后读一次 RTC，换算为 Unix epoch 秒（含 `days_from_civil` 类民用日期到秒的转换）记为 `boot_unix_time`，并记录 `boot_tick = timer::ticks()`。
- [x] 2.2 实现只读查询 API：`boot_unix_time()` 与 `current_unix_time() = boot_unix_time + (ticks() - boot_tick) / TIMER_HZ`，保证查询路径不触硬件、不分配、不阻塞且单调不减。
- [x] 2.3 实现 RTC 无效时确定性退化：`boot_unix_time = 0`，发射固定 marker（如 `BIGOS_RTC_INVALID`）到 COM1/VGA，绝不 panic/阻塞/分配。
- [x] 2.4 在内核启动序列中（timer init 之后、`proc::init()` 之前）调用 `time::init()`；确认初始化顺序正确、墙钟基准在进程创建前就绪。

## 3. 进程身份与权限原语

- [x] 3.1 在 [proc.h](include/bigos/proc.h) 的 `Process` 增加 `uint32_t uid, gid, euid, egid;` 与 `int64_t start_unix_time;`，保持为追加字段、不破坏既有布局假设。
- [x] 3.2 在进程创建路径初始化身份：init 与非 fork ELF 创建路径置 root（全 0）、`start_unix_time = current_unix_time()`；exec 不改变身份、不刷新时间戳。
- [x] 3.3 在 `fork_current` 中让子进程逐字段继承父进程 uid/gid/euid/egid，`start_unix_time` 取 fork 时墙钟；确认不引入新的分配失败点、不改变父返回子 PID/子返回 0 与 COW/引用计数/回滚语义。
- [x] 3.4 实现进程特权操作判定纯函数 `may_signal(actor, target)`：euid==0 放行任意目标，否则要求身份匹配，非法/空指针输入返回拒绝、无副作用。
- [x] 3.5 实现文件 owner/mode 访问判定纯函数与权限位常量（owner/group/other 的 r/w/x，复用 POSIX 数值布局）：root 全放行，否则按 owner/group/other 匹配，非法输入返回拒绝。
- [x] 3.6 内存/生命周期审查：身份字段初始化覆盖所有创建路径、fork 继承正确、判定函数无副作用、无新增分配失败路径。

## 4. 只读身份/时间查询 syscall

- [x] 4.1 在 [syscall.h](include/bigos/syscall.h) 的 `SyscallNumber` 末尾追加 `SYS_GET_TIME = 11`、`SYS_GETPID = 12`、`SYS_GETPPID = 13`、`SYS_GETUID = 14`、`SYS_GETGID = 15`，不改动既有号位与寄存器 ABI 注释。
- [x] 4.2 在 [syscall.cc](src/kernel/syscall/syscall.cc) 的 `dispatch` 增加对应分支：`SYS_GET_TIME` 回写 `current_unix_time()`，其余回写当前进程 pid/parent_pid/uid/gid；全部只读、不发 EOI、不阻塞、不分配。
- [x] 4.3 中断/ABI 审查：确认新增分支不发送 i8259 EOI、不改变 `InterruptFrame` 用法与 rax 返回约定，向量布局与 DPL 设置不变。

## 5. 验证开关与 smoke

- [x] 5.1 在 `xmake.lua` 新增默认关闭开关 `time_identity_smoke`（定义 `BIGOS_TIME_IDENTITY_SMOKE`），保留现有 smoke 矩阵不删除。
- [x] 5.2 实现默认关闭的 smoke 路径：发射 `BIGOS_TIME_IDENTITY_PASSED`/`BIGOS_TIME_IDENTITY_FAILED`，覆盖「墙钟随 tick 单调推进」「init 为 root 且 fork 子进程继承身份」「特权判定 root 放行/非匹配拒绝」「RTC 无效确定性退化」。

## 6. C++ 辅助静态检查

- [x] 6.1 对新增/修改的 C++ 源与头运行 clang 与 clangd 辅助静态检查，尽量贴近 GCC 交叉构建环境（freestanding C++17、x86_64 目标、项目 include 路径、无 hosted 运行时/异常/RTTI）；若等价 flag 不可用则记录差距与残留风险。
- [x] 6.2 修复本次变更引入的 clang/clangd 错误，确认或修复有效新增告警；验证记录区分历史诊断、本次变更诊断与工具链/freestanding 误报。

## 7. 构建与运行时验证

- [x] 7.1 运行最窄可用构建（`xmake`）确认 RTC 驱动、time 模块、Process 字段、syscall 分支编译通过；clang/clangd 仅作辅助信号，不替代 x86_64-elf-gcc 构建。
- [x] 7.2 在可用时运行 QEMU headless serial-marker smoke（`uv run python tools/boot_debug.py run --emulator qemu --display none ...` 并配 `--time_identity_smoke=y`），断言 `BIGOS_TIME_IDENTITY_PASSED`；可选 Bochs 交叉验证 RTC 端口行为。
- [x] 7.3 若 QEMU/Bochs、ROM/显示、交叉工具链或磁盘镜像不可用，显式记录缺失工具、跳过的验证、替代检查与残留风险，不得声称已做运行时验证。

## 8. 源码契约/行为断言测试

- [x] 8.1 用 `uv run pytest` 增补源码契约/行为断言测试（沿用阶段 14.5 启动的行为断言轨道）：覆盖新增 syscall 号位固定、Process 身份字段、墙钟与判定函数存在性，以及 smoke marker 行为断言。
- [x] 8.2 对新增/修改的 Python 文件运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`，修复新引入的 lint/类型/格式/测试问题；若 `uv` 不可用则显式记录该阻塞。

## 9. 文档与验证记录

- [x] 9.1 更新相关文档（`docs/en` 为 canonical，`docs/zh` 同步匹配相对路径）：记录墙钟语义（启动基准 + 单调推进、UTC、无同步）、身份/权限原语、新增 syscall 号与 `time_identity_smoke`。
- [x] 9.2 整理验证记录：分别列出已通过检查、因依赖缺失无法运行的检查与原因及残留风险、历史诊断、本次变更引入的问题；明确身份当前全为 root、墙钟会随时间漂移等已知限制。
