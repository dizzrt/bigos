## Purpose

定义 BigOS normal boot 默认进入用户态 init 的能力：在不依赖任何 smoke 构建开关的
默认构建中，内核在进程子系统初始化完成后、调度器启动前，复用现有 VFS 与 ELF 加载
路径加载并运行 `/boot/user/init.elf` 用户进程，使「跑完一个真实 ring3 用户程序」成为
默认路径上被持续运行、可被行为断言验证的契约。本能力同时规定 init 缺失/非法时的
确定性降级、init 退出与回收行为，以及对既有 boot/ABI 契约和 smoke 开关的保留约束。

## Requirements

### Requirement: Normal boot 默认进入用户态 init

BigOS 的 normal boot SHALL 在 `proc::init()` 完成之后、`sched::start()` 之前，
执行一个默认开启、无 `#ifdef` 构建守卫的 `launch_init` 入口，复用现有 VFS 与 ELF
加载路径加载并运行 `/boot/user/init.elf`，使 ring3 进入成为默认路径上被持续运行的
行为，而不再仅依赖 `user_program_smoke` / `user_elf_smoke` 开关。

#### Scenario: 默认构建进入 ring3

- **WHEN** BigOS 以默认配置（不开启任何 smoke 开关）构建并启动
- **THEN** 内核 MUST 在 `proc::init()` 与 `sched::start()` 之间调用 `launch_init`
- **AND** `launch_init` MUST 通过 `vfs::init` -> 读取 `/boot/user/init.elf` ->
  `create_elf_user_process` -> `run_user_process` 进入 ring3 用户进程
- **AND** 内核 MUST 在进入用户进程前发出 `BIGOS_INIT_ENTER` 串口 marker

#### Scenario: launch_init 无 #ifdef 守卫

- **WHEN** 审查 `launch_init` 的源码与其调用点
- **THEN** `launch_init` 的定义与调用 MUST NOT 被任何 smoke 相关的 `#ifdef`
  构建开关包裹
- **AND** `/boot/user/init.elf` 用户态二进制 MUST 在默认构建中被打包进引导镜像，
  而不仅在 `user_elf_smoke` 构建中存在

### Requirement: init 缺失或非法时确定性降级

`launch_init` SHALL 在 `/boot/user/init.elf` 缺失、大小超出 bounded 上限、或不是
可加载的 ELF64 `ET_EXEC` 镜像时，走确定性降级路径：发出带 `BIGOS_INIT_*` 前缀的诊断
marker 并进入统一 panic 路径（PID-1 语义雏形），而不是静默继续启动或进入未定义状态。

#### Scenario: init.elf 缺失

- **WHEN** `launch_init` 通过 VFS 打开 `/boot/user/init.elf` 失败
- **THEN** 内核 MUST 发出带 `BIGOS_INIT_*` 前缀且包含失败原因的诊断 marker
- **AND** 内核 MUST 进入统一 panic 路径，而不是继续到 `sched::start()` 的空闲态

#### Scenario: init.elf 非法或超限

- **WHEN** `/boot/user/init.elf` 大小为 0、超过 bounded 文件上限、或
  `create_elf_user_process` 返回失败
- **THEN** 内核 MUST 发出带 `BIGOS_INIT_*` 前缀且标识具体失败原因的诊断 marker
- **AND** 内核 MUST 进入统一 panic 路径

### Requirement: init 退出与回收行为确定

BigOS SHALL 定义并实现 init 用户进程正常退出或被 reaper 回收之后的确定性内核行为，
并通过串口 marker 暴露该转换，使「normal boot 跑完一个真实用户程序」成为可被行为
断言验证的契约。

#### Scenario: init 正常退出

- **WHEN** init 用户进程通过 `exit` 系统调用结束
- **THEN** 内核 MUST 发出 `BIGOS_INIT_EXIT` 串口 marker
- **AND** 内核 MUST 在发出该 marker 后进入现有 idle 调度（halt），而不是 panic 或进入
  未定义状态

#### Scenario: init 被安全回收

- **WHEN** init 进程退出后被现有的 zombie/reaper teardown 流程回收
- **THEN** 回收 MUST 复用现有进程生命周期 teardown 路径
- **AND** 内核 MUST NOT 因 init 是首个/PID-1 进程而泄漏地址空间或破坏调度器不变量

### Requirement: 默认进入 init 保留既有契约与 smoke 开关

引入默认 init 启动 SHALL NOT 改动既有 boot 布局、内核入口契约、中断/syscall ABI、
磁盘布局或既有 smoke marker，且 SHALL NOT 删除 `user_program_smoke` /
`user_elf_smoke` 开关。

#### Scenario: 既有 smoke 开关保留

- **WHEN** 默认 init 启动落地之后
- **THEN** `user_program_smoke` 与 `user_elf_smoke` 开关 MUST 仍可用作额外验证路径
- **AND** 它们原有的 `BIGOS_USER_ENTER` / `BIGOS_USER_EXIT` 等 marker 行为 MUST 保持不变

#### Scenario: 低层契约保持不变

- **WHEN** 本 change 新增 `launch_init` 及其打包
- **THEN** 它 MUST NOT 改动内核链接地址、BootInfo/handoff ABI、页表布局假设、IDT
  向量、IRQ EOI 规则、syscall 向量 `0x80`、CR3 切换规则或既有 smoke marker 字符串
- **AND** 它 MUST NOT 引入新的 syscall 或放宽 ELF 加载器的 bounded 限制
