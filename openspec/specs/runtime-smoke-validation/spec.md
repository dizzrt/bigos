## Purpose

Define productized runtime smoke validation for BigOS, including an explicit smoke matrix, QEMU headless marker checks, structured validation artifacts, and scenario-specific low-level cross-validation guidance.
## Requirements

### Requirement: runtime smoke 默认输出使用 logs
BigOS SHALL use `logs/` as the default repository-level directory for runtime smoke validation artifacts and per-case serial logs. The runtime smoke runner MUST keep explicit `--output` and `--serial-log-dir` values unchanged when developers provide custom paths.

#### Scenario: Matrix artifact 默认写入 logs
- **WHEN** the runtime smoke matrix runner is invoked without an explicit `--output`
- **THEN** it MUST write the Markdown-first validation artifact under `logs/`
- **AND** it MUST NOT use `log/runtime-smoke-validation.md` as the default output path

#### Scenario: Matrix per-case 串口日志默认写入 logs
- **WHEN** the runtime smoke matrix runner is invoked without an explicit `--serial-log-dir`
- **THEN** it MUST write per-case serial logs under `logs/runtime-smoke/`
- **AND** each validation result MUST record the resulting `logs/` serial log path

#### Scenario: 自定义 runtime smoke 输出路径不被改写
- **WHEN** a developer passes an explicit `--output` or `--serial-log-dir`
- **THEN** the runtime smoke runner MUST use the provided path
- **AND** it MUST NOT silently rewrite custom paths that still include `log/`

#### Scenario: runtime smoke 文档同步 logs 默认值
- **WHEN** runtime smoke validation documentation describes default artifact paths, serial log directories, or example commands
- **THEN** it MUST describe the new `logs/` defaults
- **AND** matching `docs/en` and `docs/zh` runtime smoke pages MUST stay synchronized

### Requirement: Runtime smoke matrix is explicit

BigOS SHALL provide an explicit runtime smoke validation matrix that lists each supported smoke case, the xmake switches required for that case, the preferred emulator path, the expected serial markers, the case-specific timeout, and the generated log or artifact paths.

#### Scenario: Matrix lists narrow smoke cases

- **WHEN** a developer inspects the runtime smoke matrix
- **THEN** the matrix MUST include narrow cases for memory self-test, timer IRQ, scheduler, syscall, read-only filesystem, first user program, and filesystem-backed user ELF validation
- **AND** each case MUST list only the smoke switches needed for that case instead of enabling every smoke switch at once
- **AND** filesystem and filesystem-backed user ELF cases MUST be able to use longer default timeouts than fast memory, timer, scheduler, or syscall cases

#### Scenario: Matrix preserves smoke defaults

- **WHEN** BigOS is built or booted outside the matrix runner
- **THEN** all runtime smoke options MUST remain default-off unless the developer explicitly configures them with `xmake f ...=y`

#### Scenario: User-mode smoke boundaries are visible

- **WHEN** the matrix lists `user_program_smoke` or `user_elf_smoke`
- **THEN** it MUST identify that these cases compile `kernel/core/proc/**` and are not part of a normal boot configuration

### Requirement: Runtime smoke runner uses QEMU headless marker checks

BigOS SHALL provide a validation runner as a `tools/boot_debug.py` subcommand or equivalent documented helper flow that executes matrix cases through the existing Legacy BIOS/MBR/exFAT image path and prefers QEMU headless serial-marker checks for automated validation.

#### Scenario: Runner executes a matrix case

- **WHEN** the runner executes a runtime smoke matrix case
- **THEN** it MUST configure the case-specific smoke switches through `xmake f`
- **AND** it MUST build the configured kernel and boot artifacts through the xmake-backed flow
- **AND** it MUST launch the QEMU backend with headless display mode or an equivalent `tools/boot_debug.py` QEMU headless helper path
- **AND** it MUST wait for the case's expected serial marker within the case-specific bounded timeout

#### Scenario: Expected marker is observed

- **WHEN** the runner observes the expected serial marker for a case
- **THEN** it MUST mark that case as passed
- **AND** it MUST record the serial log path and observed marker in the validation artifact

#### Scenario: Expected marker is missing

- **WHEN** the runner does not observe the expected serial marker before timeout or emulator exit
- **THEN** it MUST mark that case as failed
- **AND** it MUST record the missing marker, serial log path, timeout or exit status, and failed stage

### Requirement: Validation artifact records executed and skipped checks

BigOS SHALL generate a Markdown-first structured validation artifact for the runtime smoke matrix that records the environment, matrix case results, logs, skipped checks, and residual risk in a reviewable format with JSON schema compatible fields.

#### Scenario: Validation artifact is generated

- **WHEN** a runtime smoke matrix run completes or stops after a case failure
- **THEN** the runner MUST write a validation artifact under `build/test/` or an explicitly specified output path
- **AND** the artifact MUST include tool availability, xmake configuration for each case, expected markers, observed markers, serial log paths, case-specific timeout, case status, and residual risk notes

#### Scenario: Required tool is unavailable

- **WHEN** `uv`, `xmake`, a required `x86_64-elf-*` tool, QEMU, Bochs, ROM/display configuration, or another required local dependency is unavailable
- **THEN** the artifact MUST mark affected cases as skipped or blocked rather than passed
- **AND** it MUST record the missing dependency, alternative checks that were executed, and remaining validation risk

#### Scenario: Manual validation is recorded

- **WHEN** a developer performs a documented single-case smoke manually instead of using the matrix runner
- **THEN** the validation artifact or review notes MUST record the command, smoke switches, expected marker, serial log path, result, and any skipped matrix cases

### Requirement: Low-level cross-validation is scenario-specific

BigOS SHALL keep Bochs or QEMU+Bochs cross-validation as a scenario-specific recommendation for boot, IRQ, timer, ATA PIO, port IO, and hardware-behavior changes, without making Bochs mandatory for every automated smoke case.

#### Scenario: High-risk low-level change uses cross-validation when available

- **WHEN** a change affects boot, real-mode/protected-mode/long-mode transition, interrupt dispatch, timer IRQ, ATA PIO, port IO, or low-level driver behavior
- **THEN** validation MUST include Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** the validation artifact MUST record the emulator backend, display mode, serial log or diagnostic output, and result

#### Scenario: Bochs cross-validation is unavailable

- **WHEN** Bochs, ROM paths, display configuration, or host emulator setup prevents cross-validation
- **THEN** the validation artifact MUST record why Bochs validation was skipped
- **AND** it MUST identify which QEMU, build, source-level, or manual checks were used as substitutes and what residual hardware-behavior risk remains

### Requirement: Runtime smoke productization preserves kernel contracts

Runtime smoke validation productization SHALL NOT change existing boot layout, kernel entry contracts, interrupt/syscall ABI, disk layout, smoke marker strings, or smoke-only user process boundaries.

#### Scenario: Legacy boot path is preserved

- **WHEN** the matrix runner prepares and boots a smoke case
- **THEN** the image MUST continue to use the existing Legacy BIOS/MBR/exFAT raw image path with `/boot/boot.bin`, root `kernel`, and IDE-compatible disk exposure
- **AND** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, or a new storage driver

#### Scenario: Runtime ABI is preserved

- **WHEN** this change adds validation tooling or documentation
- **THEN** it MUST NOT change kernel link addresses, BootInfo/handoff ABI, page-table layout assumptions, IDT vectors, IRQ EOI rules, syscall vector `0x80`, CR3 switching rules, or existing smoke marker strings

#### Scenario: Smoke failures remain observable

- **WHEN** a smoke case fails in the kernel or during boot
- **THEN** the existing COM1/VGA marker and panic behavior MUST remain the source of truth for the runner
- **AND** the runner MUST NOT reinterpret a missing marker as success

### Requirement: Runtime smoke matrix covers blocking primitives
BigOS SHALL extend the runtime smoke validation matrix with narrow blocking primitive cases that validate wait queue wakeup, timeout wait, and optional TTY blocking behavior without enabling unrelated smoke switches.

#### Scenario: Matrix lists blocking primitive cases
- **WHEN** a developer inspects the runtime smoke matrix after blocking primitives are available
- **THEN** the matrix MUST include at least one narrow blocking primitives case that exercises thread block/wakeup and timeout wait
- **AND** it MUST list the xmake switches, expected serial markers, case-specific timeout, generated log paths, and whether TTY blocking input is synthetic, manual, skipped, or blocked

#### Scenario: Blocking smoke preserves defaults
- **WHEN** BigOS is built or booted outside the matrix runner
- **THEN** blocking primitive smoke options MUST remain default-off unless explicitly configured with `xmake f ...=y`
- **AND** existing memory, timer, scheduler, syscall, filesystem, and user-mode smoke defaults MUST remain unchanged

### Requirement: Blocking validation records low-level residual risk
BigOS SHALL record executed and skipped blocking validation in the structured runtime validation artifact.

#### Scenario: Blocking smoke passes
- **WHEN** the runner observes all expected blocking primitive serial markers within the bounded timeout
- **THEN** the validation artifact MUST record the case as passed
- **AND** it MUST include the configured switches, observed markers, serial log path, timeout, emulator backend, and any cross-validation notes

#### Scenario: Blocking smoke is skipped or blocked
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display setup, serial logging, disk image generation, or keyboard input capability is unavailable
- **THEN** the artifact MUST mark affected blocking cases as skipped or blocked rather than passed
- **AND** it MUST record substitute source/build checks and residual scheduler/timer/IRQ behavior risk

#### Scenario: IRQ and timer changes keep cross-validation guidance
- **WHEN** blocking primitive implementation changes timer IRQ, keyboard IRQ, i8259 EOI boundaries, port IO, or scheduler-adjacent IRQ hooks
- **THEN** validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** if cross-validation is unavailable, the artifact MUST explain why it was skipped

### Requirement: 运行时 smoke 矩阵覆盖默认 init 行为断言

BigOS SHALL 在 runtime smoke validation matrix 运行时 smoke 矩阵中新增一个**默认构建**（不开启任何 smoke
开关）的用例，断言 normal boot 默认进入用户态 init 的行为，并以此启动「行为断言测试」
轨道——验证逐步从源码字符串契约转向基于串口 marker 与用户态二进制输出的行为断言。

#### Scenario: 矩阵包含默认 init 用例

- **WHEN** 开发者在 behavior assertion validation baseline 之后查看运行时 smoke 矩阵
- **THEN** 矩阵 MUST 包含一个不依赖任何 smoke 开关的默认构建用例
- **AND** 该用例 MUST 断言默认构建发出 `BIGOS_INIT_ENTER` 与 `BIGOS_INIT_EXIT` 串口 marker
- **AND** 该用例 MUST 列出（空的）所需 smoke 开关、首选 QEMU headless 路径、期望 marker、
  用例专属超时以及生成的日志/artifact 路径

#### Scenario: 默认 init 用例采用行为断言

- **WHEN** 运行器执行默认 init 用例
- **THEN** 它 MUST 以内核 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT` 串口 marker 作为通过
  判据，而非断言内核 C++ 源码字符串
- **AND** 缺失期望 marker MUST 被判定为失败，而不能被重新解读为通过
- **AND** init 二进制自身的 stdout 输出断言 MUST 留待后续阶段引入，本 change 不要求

#### Scenario: 默认 init 用例不改变其他 smoke 默认值

- **WHEN** 默认 init 用例加入矩阵
- **THEN** 既有 memory、timer、scheduler、syscall、filesystem、blocking、user_program、
  user_elf 等用例的 smoke 默认值 MUST 保持不变
- **AND** `user_program_smoke` / `user_elf_smoke` 用例 MUST 仍作为额外验证路径保留在矩阵中

### Requirement: Scheduler semantics validation records IRQ risksmoke matrix covers scheduler semantics
BigOS SHALL extend the runtime smoke validation matrix with narrow scheduler semantics cases that validate timer-driven preemption, preemption-disable behavior, and preservation of existing cooperative scheduler behavior.

#### Scenario: Matrix lists scheduler semantics cases
- **WHEN** a developer inspects the runtime smoke matrix after guarded scheduler semantics are available
- **THEN** the matrix MUST include at least one narrow scheduler semantics case that exercises time slice expiry and timer-driven reschedule-on-IRQ-return
- **AND** it MUST list the xmake switches, expected serial markers, case-specific timeout, generated log paths, and required emulator backend for the case

#### Scenario: Matrix keeps unrelated smokes default-off
- **WHEN** scheduler semantics smoke is configured
- **THEN** unrelated memory, filesystem, user program, user ELF, and broad smoke options MUST remain disabled unless explicitly required by the case
- **AND** all smoke options MUST remain default-off outside explicit `xmake f ...=y` configuration

#### Scenario: Cooperative scheduler smoke remains meaningful
- **WHEN** the runtime smoke matrix includes both cooperative scheduler and preemptive scheduler semantics cases
- **THEN** the existing cooperative marker expectations MUST remain documented
- **AND** the preemption case MUST use distinct markers or artifact fields so validation can distinguish explicit yield from timer-driven rescheduling

### Requirement: Scheduler semantics validation records IRQ risk
BigOS SHALL record executed and skipped scheduler semantics validation in the structured runtime validation artifact, including low-level IRQ/timer/context-switch residual risk.

#### Scenario: Preemption smoke passes
- **WHEN** the runner observes all expected scheduler semantics serial markers within the bounded timeout
- **THEN** the validation artifact MUST record the case as passed
- **AND** it MUST include configured switches, observed markers, serial log path, timeout, emulator backend, and whether Bochs or QEMU+Bochs cross-validation was executed

#### Scenario: Preemption smoke is skipped or blocked
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display setup, serial logging, disk image generation, or scheduler smoke prerequisites are unavailable
- **THEN** the artifact MUST mark affected scheduler semantics cases as skipped or blocked rather than passed
- **AND** it MUST record substitute source/build checks and residual scheduler/timer/IRQ behavior risk

#### Scenario: Low-level cross-validation is recommended
- **WHEN** implementation changes timer IRQ, i8259 EOI ordering, ISR assembly, interrupt dispatch, context-switch assembly, port IO assumptions, or scheduler-adjacent IRQ hooks
- **THEN** validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when the local environment supports it
- **AND** if cross-validation is unavailable, the artifact MUST explain why it was skipped

### Requirement: 用户态运行时验证开关 userland_smoke

BigOS SHALL 新增一个默认关闭的构建开关 `userland_smoke`（定义 `BIGOS_USERLAND_SMOKE`），用于在受控构建中验证用户态运行时端到端路径，并发射固定 COM1/VGA marker `BIGOS_USERLAND_PASSED` 或 `BIGOS_USERLAND_FAILED`。该开关 MUST 默认关闭，MUST NOT 改动或删除既有 smoke 开关与其 marker，且 MUST NOT 成为 normal boot 的一部分。

#### Scenario: 开关默认关闭

- **WHEN** BigOS 以默认配置构建
- **THEN** `BIGOS_USERLAND_SMOKE` MUST NOT 被定义
- **AND** 既有 smoke 开关与 marker 行为 MUST 保持不变

#### Scenario: 开启后发射通过 marker

- **WHEN** 以 `userland_smoke=y` 构建并在 QEMU headless 下启动，且用户态运行时路径（crt0 传参与退出码、libc syscall wrapper 与 errno 翻译、shell `fork`+`execve`+`wait`、单级管道与重定向、最小 `malloc`/`free`）全部通过
- **THEN** 内核或用户态验证程序 MUST 发射 `BIGOS_USERLAND_PASSED`

#### Scenario: 失败时发射失败 marker

- **WHEN** 用户态运行时验证路径中任一断言失败
- **THEN** MUST 发射 `BIGOS_USERLAND_FAILED`（可附带失败原因）
- **AND** MUST NOT 静默通过或发射 `BIGOS_USERLAND_PASSED`

### Requirement: interactive console usability validation preserves headless behavior assertions
BigOS SHALL validate interactive console usability without making graphical display, manual keyboard input, or emulator scancode injection mandatory for every automated smoke run.

#### Scenario: Headless default boot remains observable
- **WHEN** interactive console usability changes are validated through the preferred QEMU headless serial/log path
- **THEN** validation MUST continue to assert the default userland/init behavior through existing deterministic serial/log observations
- **AND** missing expected observations MUST be recorded as failure rather than reinterpreted as success

#### Scenario: Interactive console evidence is layered
- **WHEN** graphical QEMU, Bochs, manual keyboard input, or emulator keyboard injection is available
- **THEN** validation SHOULD record evidence that prompt, typed input echo, and command output are visible on the runtime text console
- **AND** the evidence MUST identify emulator backend, display/input method, executed command, observed output, and result

#### Scenario: Manual input capability is unavailable
- **WHEN** local display, ROM, keyboard input, emulator injection, disk image generation, or cross-toolchain setup prevents interactive console validation
- **THEN** validation MUST mark the interactive portion as skipped or blocked rather than passed
- **AND** validation MUST record substitute source/build/headless checks and the remaining console-usability risk

### Requirement: interactive console usability validation does not widen runtime boundaries
BigOS SHALL keep interactive console usability validation within the current bounded userland and x86_64 Legacy BIOS runtime boundary.

#### Scenario: Existing runtime contracts are preserved
- **WHEN** interactive console validation is added or executed
- **THEN** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, dynamic linking, full POSIX terminal support, job control, or a complete POSIX libc
- **AND** it MUST NOT change boot layout, kernel link addresses, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or existing smoke marker semantics

### Requirement: 简单 C 程序行为断言覆盖 简单 C 程序基线

BigOS runtime validation SHALL provide behavior-oriented checks for the simple C program baseline. These checks MUST validate runtime-observable behavior such as argument handoff, environment handoff, stdout/stderr output, `errno` translation, process exit status, and shell execution of packaged C programs.

#### Scenario: 参数和环境行为可验证

- **WHEN** simple C program baseline runtime validation runs the simple C program baseline
- **THEN** validation MUST observe that a packaged C program receives expected `argc`/`argv`
- **AND** validation MUST observe that environment handoff is present or deterministically reported as absent within the documented boundary

#### Scenario: wrapper 和错误报告行为可验证

- **WHEN** simple C program baseline runtime validation exercises a failing libc wrapper path
- **THEN** validation MUST observe the documented failure return and `errno` behavior through program output, exit status, or another runtime-visible result

#### Scenario: shell 执行小型 C 程序可验证

- **WHEN** simple C program baseline runtime validation invokes packaged C programs through `/bin/sh` or an equivalent deterministic shell path
- **THEN** validation MUST observe program stdout/stderr and exit behavior
- **AND** validation MUST confirm the shell continues after the external program exits

### Requirement: 简单 C 程序基线验证保持分层和默认关闭

BigOS SHALL keep simple C program baseline validation layered with existing source, build, runtime, and environment-dependent checks. Emulator-dependent validation MUST remain optional or default-off unless the surrounding test mode explicitly enables it.

#### Scenario: 默认构建不强制运行 emulator smoke

- **WHEN** a normal default build is requested
- **THEN** simple C program baseline emulator smoke MUST NOT be mandatory for the build to complete

#### Scenario: 环境缺失时记录残留风险

- **WHEN** QEMU、Bochs、交叉工具链、显示或串口日志环境不可用
- **THEN** validation notes MUST record the skipped check, substitute checks, and residual risk instead of claiming runtime validation passed

### Requirement: 运行时文件系统行为验证可复现

BigOS SHALL 为有界运行时文件系统可用性提供分层验证：OpenSpec/文档一致性、源码或构建检查、用户态 C 程序行为检查、shell 重定向检查，以及环境可用时的 QEMU/Bochs 运行时 smoke。验证 MUST 记录工具链、emulator、ROM/display、磁盘镜像和 `uv` 可用性；环境依赖不可满足时 MUST 记录跳过原因、替代检查和残余风险。

#### Scenario: 用户程序验证覆盖文件操作
- **WHEN** 运行运行时文件系统行为验证
- **THEN** 验证 MUST 覆盖创建、写入、seek、读回、fsync、mkdir、最小目录枚举、unlink、只读后端拒写和容量/权限/非法路径失败中的代表性路径
- **AND** 结果 MUST 能通过用户程序输出、退出状态、串口日志或确定性测试报告判断

#### Scenario: 优先复用 userland smoke
- **WHEN** 增加运行时文件系统行为验证
- **THEN** 验证 MUST 优先复用或扩展现有 userland smoke 的打包、启动和可观察输出路径
- **AND** 只有在复用导致用例耦合过大时才拆出小型专用用户程序

#### Scenario: shell 验证覆盖重定向
- **WHEN** 运行 shell 或 userland 组合验证
- **THEN** 验证 MUST 覆盖至少一个输出重定向到 `/rw` 文件并读回的路径
- **AND** 失败重定向 MUST 被观察为确定性错误而不是 shell 崩溃

### Requirement: 验证记录区分通过、跳过和残余风险

BigOS SHALL 在运行时文件系统可用性验证记录中区分已通过检查、因环境不可用跳过的检查、历史诊断、当前变更引入的问题和残余风险。涉及 Python 辅助脚本时 MUST 通过 `uv run ...` 执行；`uv` 不可用时 MUST 明确记录阻塞而不是静默使用系统 Python。

#### Scenario: emulator 不可用时记录跳过
- **WHEN** QEMU、Bochs、cross-toolchain、ROM/display、磁盘镜像或串口 oracle 不可用
- **THEN** 对应运行时 smoke MAY 被跳过
- **AND** 验证记录 MUST 标明缺失条件、已执行替代检查和剩余 bootability 或行为风险

#### Scenario: Python 辅助验证遵守 uv 约定
- **WHEN** 运行 Python helper、pytest、ruff 或 pyright 相关验证
- **THEN** 命令 MUST 使用 `uv run ...`
- **AND** 若 `uv` 不可用，验证记录 MUST 明确该 blocker

### Requirement: 行为导向验证矩阵覆盖最小可用系统

BigOS runtime validation SHALL provide a behavior-oriented validation matrix for the minimal usable system. The matrix MUST cover representative runtime-observable checks for interactive shell behavior, simple C programs, process/fd semantics, runtime filesystem operations, and bounded userland compatibility. Each behavior check MUST identify the exercised capability, expected observable result, failure signal, and validation layer.

#### Scenario: 默认 userland 行为可观察

- **WHEN** behavior-oriented validation runs the default init and shell path in a configured QEMU headless environment
- **THEN** validation MUST observe that userland reaches the documented shell or init behavior through deterministic serial/log output, command output, exit status, or an equivalent low-level signal
- **AND** missing expected observations MUST be recorded as failure rather than success

#### Scenario: shell 和简单 C 程序行为可判定

- **WHEN** behavior-oriented validation exercises supported shell commands and packaged simple C programs
- **THEN** validation MUST assert expected stdout/stderr, exit status, argument handoff, environment handling, and shell continuation behavior
- **AND** the assertions MUST remain within the bounded shell and minimal libc subset documented for BigOS

#### Scenario: runtime filesystem 行为可复现

- **WHEN** behavior-oriented validation exercises supported runtime filesystem operations
- **THEN** validation MUST assert observable file creation, read, write, seek, sync, directory, or removal effects through file contents, return values, error paths, or command output
- **AND** validation MUST NOT imply support for persistent full writable filesystems, broad file-backed mapping, async I/O, or broad storage/device support

### Requirement: 行为验证保持分层且记录环境缺失

BigOS SHALL keep behavior-oriented validation layered across source/spec consistency, build and packaging checks, QEMU headless runtime assertions, and optional graphical, Bochs, manual input, or hardware-adjacent evidence. Environment-dependent validation MAY be skipped only when the result records the missing dependency, substitute checks, and residual risk.

#### Scenario: 环境依赖检查缺失时不冒充通过

- **WHEN** QEMU, Bochs, the x86_64 cross-toolchain, display/ROM dependencies, serial logging, keyboard input, or disk image configuration are unavailable
- **THEN** the corresponding behavior validation MUST be marked skipped or blocked instead of passed
- **AND** the validation record MUST identify the unavailable dependency, substitute checks that were run, and the remaining risk

#### Scenario: 场景化低层交叉验证保持可选

- **WHEN** a change affects boot, IRQ, timer, ATA PIO, port IO, display input, or other hardware-sensitive behavior
- **THEN** Bochs or QEMU plus Bochs cross-validation SHOULD be recorded when available
- **AND** absence of that cross-validation MUST NOT block unrelated source-only or documentation-only changes when substitute checks and residual risk are recorded

#### Scenario: 行为验证不扩大运行时边界

- **WHEN** behavior-oriented validation is added, documented, or executed
- **THEN** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, dynamic linking, full POSIX terminal support, job control, or a complete POSIX libc
- **AND** it MUST NOT change boot layout, kernel link addresses, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or existing ABI assumptions

### Requirement: Runtime validation covers shell usability hardening

BigOS SHALL provide behavior-oriented validation for hardened shell usability by extending the existing `userland_smoke` path rather than adding a separate shell usability smoke switch. Validation MUST cover at least one successful command path, one deterministic command or syntax failure, one cwd-relative path command, one output redirection path, one single-pipe composition path, one bounded exit-status observation path, and recovery by running a subsequent command after failure. Validation MUST preserve existing runtime smoke defaults and MUST NOT require unrelated smoke switches unless explicitly documented by the selected case.

#### Scenario: 验证覆盖成功和失败 shell 行为

- **WHEN** shell usability validation runs in an environment with required cross-toolchain, image packaging, shell/userland, and emulator support
- **THEN** it MUST observe a supported command success, a deterministic unsupported or missing-command failure, and a subsequent command that proves shell recovery
- **AND** each result MUST be decidable through stdout/stderr, bounded exit status, serial/log output, or another deterministic low-level signal

#### Scenario: 验证覆盖 redirection 与 pipe 组合

- **WHEN** shell usability validation exercises supported redirection and single-pipe behavior
- **THEN** it MUST observe that redirected output reaches the intended writable runtime path or pipe data reaches the downstream command
- **AND** unrelated parent shell fd state MUST remain usable for a later observable command

#### Scenario: 验证覆盖 bounded status

- **WHEN** validation runs commands that succeed, fail during shell setup, fail during exec, or exit nonzero
- **THEN** the validation path MUST distinguish success from failure through a shell-local bounded status or deterministic output
- **AND** it MUST NOT require full POSIX `$?`, shell variables, scripts, job control, or terminal process groups

#### Scenario: 验证扩展现有 userland smoke

- **WHEN** shell usability runtime validation is added
- **THEN** it MUST extend the existing `userland_smoke` coverage instead of requiring a new shell usability smoke switch
- **AND** the validation matrix or notes MUST describe which shell usability cases are covered by that existing smoke path

### Requirement: Shell usability validation records environment-dependent skips

BigOS SHALL record executed, skipped, and blocked shell usability checks in reviewable validation artifacts or notes. Environment-dependent validation MAY be skipped only with an explicit record of missing `uv`, xmake, `x86_64-elf-*` toolchain, QEMU, Bochs, ROM/display setup, disk image configuration, serial oracle, timeout controls, or another required local dependency; skipped checks MUST NOT be reported as passed.

#### Scenario: 环境可用时记录执行结果

- **WHEN** shell usability runtime validation completes in a configured environment
- **THEN** the validation artifact or notes MUST record the selected case, configured switches, emulator backend, observed output or markers, log paths, timeout, and result
- **AND** it MUST identify whether QEMU, Bochs, or substitute checks were used

#### Scenario: 环境不可用时记录跳过

- **WHEN** required toolchain, emulator, display/ROM, disk image, serial oracle, or timeout dependency is unavailable
- **THEN** affected shell usability runtime checks MUST be marked skipped or blocked rather than passed
- **AND** validation notes MUST record substitute source/build/spec checks and remaining shell/userland behavior risk

#### Scenario: Validation preserves low-level contracts

- **WHEN** shell usability validation tooling or notes are added
- **THEN** they MUST NOT change boot layout, linker addresses, disk layout, IDT vectors, syscall vector `0x80`, CR3 switching rules, existing smoke marker strings, or default-off smoke behavior
- **AND** any hardware-behavior residual risk MUST be recorded if emulator cross-validation is unavailable

### Requirement: Terminal preparation validation remains layered

BigOS SHALL validate minimal terminal abstraction work through layered source/spec, build, headless runtime, and optional interactive evidence. Automated validation MUST prefer deterministic QEMU headless serial/log checks for default userland reachability and behavior-oriented assertions where available, while graphical display, manual keyboard input, emulator scancode injection, and Bochs cross-validation remain environment-dependent evidence that MUST be recorded separately.

#### Scenario: Headless validation preserves default userland checks

- **WHEN** terminal preparation changes are validated in a configured QEMU headless environment
- **THEN** validation MUST continue to observe default init/shell or userland behavior through deterministic serial/log output, command output, exit status, or another low-level signal
- **AND** missing expected observations MUST be recorded as failure rather than success

#### Scenario: Interactive evidence is optional and explicit

- **WHEN** graphical QEMU, Bochs, manual keyboard input, emulator keyboard injection, display/ROM setup, and disk image generation are available
- **THEN** validation SHOULD record evidence that prompt text, bounded input feedback, control-character behavior, and command output are visible on the runtime text console
- **AND** the evidence MUST identify emulator backend, display/input method, executed input sequence, observed output, result, and any residual risk

#### Scenario: Missing interactive environment is not a pass

- **WHEN** local display, keyboard input, emulator injection, Bochs ROM/display, QEMU, cross-toolchain, serial logging, disk image generation, or `uv` support is unavailable
- **THEN** affected interactive terminal validation MUST be marked skipped or blocked rather than passed
- **AND** validation notes MUST record substitute source/build/headless checks and remaining terminal behavior risk

### Requirement: Terminal validation preserves low-level contracts

BigOS SHALL keep terminal abstraction validation within the current bounded x86_64 Legacy BIOS runtime boundary and existing behavior-oriented validation model. Validation MUST NOT require new boot backends, new storage drivers, SMP, full POSIX terminal behavior, job control, process groups, dynamic linking, hosted libc, or changes to existing smoke defaults and marker semantics.

#### Scenario: Runtime contracts are unchanged

- **WHEN** terminal validation tooling, docs, or review notes are added
- **THEN** they MUST NOT change boot layout, linker addresses, disk layout, IDT vectors, syscall vector `0x80`, CR3 switching rules, existing smoke marker strings, default-off smoke behavior, or user/kernel ABI assumptions
- **AND** if implementation touches IRQ, keyboard, console, port IO, or hardware-adjacent behavior, validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when local environment supports it

#### Scenario: Validation record separates results

- **WHEN** terminal preparation validation is reported
- **THEN** the record MUST distinguish passed checks, skipped or blocked checks, historical diagnostics, current-change diagnostics, substitute checks, and residual risks
- **AND** Python-related helper execution, if any, MUST be described with `uv run ...`; if `uv` is unavailable, the record MUST state that blocker explicitly

### Requirement: bounded POSIX-like surface smoke coverage
BigOS SHALL provide default-off runtime smoke or equivalent source-contract validation for the bounded POSIX-like surface hardening work.

#### Scenario: Signal surface validation
- **WHEN** the bounded POSIX-like surface validation path exercises signal behavior
- **THEN** it covers installing a handler, delivering a signal, returning through the bounded sigreturn path, and preserving the default termination behavior for an unhandled signal

#### Scenario: Wait and status validation
- **WHEN** the bounded POSIX-like surface validation path exercises process waiting
- **THEN** it covers waiting for any child, waiting for a specific child, status writeback, and deterministic failure for unsupported wait options

#### Scenario: Error text validation
- **WHEN** the bounded POSIX-like surface validation path exercises libc error reporting
- **THEN** it covers errno translation, stable strerror text for known errors, fallback text for unknown errors, and perror output to stderr

#### Scenario: Shell composition validation
- **WHEN** the bounded POSIX-like surface validation path exercises shell behavior
- **THEN** it covers successful commands, command-not-found, unsupported syntax, failed redirection recovery, single-stage pipe EOF, and bounded status reporting

### Requirement: bounded POSIX-like surface source-contract validation
BigOS SHALL keep user/kernel mirror contracts synchronized for any bounded POSIX-like surface public header or wrapper that mirrors kernel syscall numbers, errno values, signal constants, wait constants, or ABI-sensitive signal frame data.

#### Scenario: Mirror constants remain synchronized
- **WHEN** source-contract validation runs
- **THEN** it detects mismatches between user-visible constants and their kernel-owned sources before runtime smoke execution

### Requirement: runtime filesystem maturity 文件系统行为验证
BigOS SHALL provide a dedicated default-off behavior-oriented runtime smoke for runtime filesystem maturity. Validation MUST cover current-runtime success and failure paths across read-only exFAT, RAM-backed `/rw`, fd/VFS operations, metadata queries, directory enumeration, cwd-relative paths, libc errno wrappers, and shell-visible user tools. Environment-dependent emulator checks MUST record missing QEMU, Bochs, cross toolchain, ROM/display, disk image, serial oracle, or timeout dependencies as skipped rather than passed.

#### Scenario: 运行期成功组合路径验证
- **WHEN** runtime filesystem maturity validation runs in an environment with the required build, toolchain, disk image, emulator, and serial observation support
- **THEN** it MUST exercise at least one read-only exFAT read/metadata path and one `/rw` create/write/read/lseek/fsync/stat/list/unlink/restricted-rename path
- **AND** it MUST verify that observed file contents, metadata, stable directory entry order, fd references, and shell/libc-visible results match the bounded filesystem contract

#### Scenario: 运行期失败组合路径验证
- **WHEN** runtime filesystem maturity validation runs
- **THEN** it MUST exercise representative failures for read-only write, missing path, existing target, invalid fd, invalid user buffer, permission denial, naturally filled `/rw` capacity exhaustion, unsupported object type, and directory enumeration output exhaustion
- **AND** it MUST verify deterministic errno behavior and state preservation for each failure class

#### Scenario: 环境不可用时记录跳过
- **WHEN** required emulator, cross toolchain, boot image, display/ROM, serial oracle, timeout, or local configuration dependencies are unavailable
- **THEN** validation notes MUST record the unavailable dependency, skipped cases, substitute checks, and residual risk
- **AND** they MUST NOT claim runtime filesystem maturity validation passed

### Requirement: 验证保持 roadmap 边界
BigOS SHALL keep runtime filesystem maturity validation aligned with the bounded userland and non-persistent `/rw` roadmap boundary. Validation MAY use source-level checks, small static C programs, shell tools, and the dedicated default-off filesystem maturity runtime smoke, but MUST NOT require dynamic linking, complete POSIX test suites, SMP, UEFI runtime parity, broad storage drivers, or cross-reboot persistence checks.

#### Scenario: 专用 smoke 不替代基础回归
- **WHEN** runtime filesystem maturity validation is added
- **THEN** BigOS MUST keep it as a dedicated default-off runtime path for cross-layer filesystem behavior
- **AND** existing writable filesystem and userland smokes MAY remain as narrower regression checks rather than carrying the full runtime filesystem maturity contract

#### Scenario: 验证不要求跨重启持久化
- **WHEN** runtime filesystem maturity validation writes files under `/rw`
- **THEN** validation MUST treat those files as current-session state only
- **AND** it MUST NOT require reboot-and-remount persistence unless a later accepted persistent-storage change adds that requirement

#### Scenario: 验证不扩大 POSIX 声明
- **WHEN** validation uses libc wrappers, shell commands, or small user tools to observe filesystem behavior
- **THEN** it MUST describe the checked behavior as a BigOS bounded filesystem subset
- **AND** it MUST NOT claim complete POSIX filesystem, shell, libc, directory stream, or metadata compatibility

### Requirement: Runtime smoke validation covers default UEFI boot
BigOS runtime smoke validation SHALL include a default UEFI boot validation path that proves the promoted default backend reaches the current bounded userland baseline through deterministic serial evidence.

#### Scenario: Matrix identifies default UEFI boot case
- **WHEN** a developer inspects the runtime smoke validation matrix after UEFI default promotion
- **THEN** the matrix MUST include a default UEFI boot case using the normal default boot configuration
- **AND** the case MUST list required backend dependencies, expected deterministic serial evidence, timeout, generated UEFI log/artifact paths, and whether the Legacy BIOS comparison path was run, skipped, or left for a later validation task

#### Scenario: Default UEFI boot case passes
- **WHEN** the runtime smoke runner or documented helper executes the default UEFI boot case under QEMU + OVMF
- **THEN** it MUST build or select the UEFI artifacts, prepare the ESP/FAT image, launch QEMU headless with OVMF, and observe the expected default userland evidence within the bounded timeout
- **AND** it MUST record the case as passed only after the bounded userland baseline is reached

#### Scenario: Default UEFI boot case fails
- **WHEN** the default UEFI boot case exits, panics, times out, or misses the expected default userland evidence
- **THEN** the validation artifact MUST record the case as failed
- **AND** it MUST include the serial log path, expected evidence, observed evidence when any, timeout or exit status, and failed stage when known

#### Scenario: Default UEFI boot case is blocked
- **WHEN** QEMU, OVMF, mtools, LLVM/LLD, xmake, the x86_64 cross toolchain, serial logging, or required image-generation support is unavailable
- **THEN** the validation artifact MUST mark the default UEFI boot case as skipped or blocked rather than passed
- **AND** it MUST record substitute build/source checks and remaining default-backend risk

### Requirement: Runtime smoke validation distinguishes backend defaults from smoke defaults
BigOS SHALL allow the default boot backend to be UEFI while preserving the existing default-off behavior of optional runtime smoke switches.

#### Scenario: Backend default changes do not enable smoke switches
- **WHEN** BigOS is built or booted with the normal default configuration after UEFI promotion
- **THEN** UEFI MUST be the default boot backend
- **AND** optional runtime smoke switches such as memory, timer, scheduler, syscall, filesystem, user program, user ELF, writable filesystem, pipe, and userland smoke MUST remain default-off unless explicitly configured

#### Scenario: Smoke-only paths remain explicit
- **WHEN** a developer enables a default-off smoke case
- **THEN** validation MUST record the selected smoke configuration separately from the selected boot backend
- **AND** the existence of a UEFI default backend MUST NOT make smoke-only user programs part of normal boot
