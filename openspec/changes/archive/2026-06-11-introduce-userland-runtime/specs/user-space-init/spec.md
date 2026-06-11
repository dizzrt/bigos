## MODIFIED Requirements

### Requirement: Normal boot 默认进入用户态 init

BigOS 的 normal boot SHALL 在 `proc::init()` 完成之后、`sched::start()` 之前，
执行一个默认开启、无 `#ifdef` 构建守卫的 `launch_init` 入口，复用现有 VFS 与 ELF
加载路径加载并运行 `/boot/user/init.elf`，使 ring3 进入成为默认路径上被持续运行的
行为，而不再仅依赖 `user_program_smoke` / `user_elf_smoke` 开关。加载成功后，init
SHALL 作为常驻 PID-1：经 `fork`+`execve` 启动 `/bin/sh`，并以 `while(1) wait(...)`
循环回收退出的子进程（含被过继到 PID-1 的孤儿/僵尸）；当 `/bin/sh` 退出时 init MUST
重新 `fork`+`execve` 拉起 shell，且 init 自身 MUST NOT 退出，使 normal boot 默认进入
并持续维持可交互的 `/bin/sh`。

#### Scenario: 默认构建进入 ring3

- **WHEN** BigOS 以默认配置（不开启任何 smoke 开关）构建并启动
- **THEN** 内核 MUST 在 `proc::init()` 与 `sched::start()` 之间调用 `launch_init`
- **AND** `launch_init` MUST 通过 `vfs::init` -> 读取 `/boot/user/init.elf` ->
  `create_elf_user_process` -> `run_user_process` 进入 ring3 用户进程
- **AND** 内核 MUST 在进入用户进程前发出 `BIGOS_INIT_ENTER` 串口 marker

#### Scenario: 默认进入交互式 shell

- **WHEN** init 用户进程在默认构建中成功进入 ring3
- **THEN** init MUST 经 `fork`+`execve`（子进程 `SYS_EXECVE`）启动 `/bin/sh`
- **AND** `/bin/sh` MUST 能从标准输入读取命令并交互运行

#### Scenario: init 常驻收割并维持 shell

- **WHEN** `/bin/sh` 退出，或有孤儿进程被过继到 PID-1
- **THEN** init MUST 经 `wait` 回收该退出/孤儿进程而不泄漏僵尸
- **AND** 当退出的是 `/bin/sh` 时 init MUST 重新 `fork`+`execve` 拉起 `/bin/sh`
- **AND** init 自身 MUST NOT 退出

#### Scenario: launch_init 无 #ifdef 守卫

- **WHEN** 审查 `launch_init` 的源码与其调用点
- **THEN** `launch_init` 的定义与调用 MUST NOT 被任何 smoke 相关的 `#ifdef`
  构建开关包裹
- **AND** `/boot/user/init.elf` 用户态二进制 MUST 在默认构建中被打包进引导镜像，
  而不仅在 `user_elf_smoke` 构建中存在
- **AND** `/bin/sh` MUST 在默认构建中被打包进引导镜像

## ADDED Requirements

### Requirement: 孤儿进程过继到 PID-1 init

BigOS SHALL 在进程退出路径提供最小的孤儿过继语义：当一个非 init 进程退出时，其仍存活或处于僵尸态的子进程 MUST 被过继给 PID-1 init（`parent_pid` 改为 init 的 pid 并挂入 init 的子进程链），使这些子进程后续的退出能被 init 的 `wait` 收割，而不是被静默自我回收或泄漏为无人收割的僵尸。被过继的子进程若已处于僵尸态，init MUST 能被唤醒并 `wait` 到它。init（PID-1）自身退出仍按现有 `BIGOS_INIT_EXIT` 与 idle/panic 边界处理，不在本需求范围内。

#### Scenario: 中间父进程退出后子进程过继给 init

- **WHEN** 一个非 init 进程退出，且它仍有未被 `wait` 的子进程
- **THEN** 这些子进程 MUST 被过继给 PID-1 init（成为 init 的子进程）
- **AND** 这些子进程后续退出时 MUST 能被 init 的 `wait` 收割而不泄漏僵尸

#### Scenario: 过继时已是僵尸的子进程唤醒 init

- **WHEN** 被过继给 init 的子进程在过继时已处于僵尸态
- **THEN** init MUST 被唤醒并能 `wait` 到该僵尸子进程并回收它

#### Scenario: init 不存在时安全回退

- **WHEN** 进程退出时 init（PID-1）已不存在（异常路径）
- **THEN** 退出路径 MUST 跳过过继并回退到现有自我回收兜底，而不崩溃或进入未定义状态
