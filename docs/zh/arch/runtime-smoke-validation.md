# 运行时 Smoke 验证

BigOS 阶段 9 将现有默认关闭的 runtime smoke 产品化为一组窄验证矩阵。该矩阵只属于 tooling 和文档层：不新增内核运行时能力、不接入 CI、不实现 UEFI、不新增存储驱动，也不改变 smoke marker ABI。

## 矩阵 Runner

- 首选自动化命令：`uv run python tools/boot_debug.py runtime-smoke-matrix`
- 单 case 命令：`uv run python tools/boot_debug.py runtime-smoke-matrix --case memory-self-test`
- Artifact 覆盖路径：`uv run python tools/boot_debug.py runtime-smoke-matrix --output build/test/runtime-smoke-validation.md`
- 串口日志：默认每个 case 一个文件，位于 `build/test/runtime-smoke/`。
- Raw image：默认每个 case 一个 Legacy BIOS/MBR/exFAT raw image，位于 `build/test/runtime-smoke/`。

runner 会通过 `xmake f` 显式配置每个 case，经由现有 xmake-backed flow 构建，准备现有 Legacy BIOS/MBR/exFAT raw image，使用 `--display none` 启动 QEMU，并在 case-specific timeout 内等待预期 COM1 marker。

## 矩阵 Case

| Case | xmake 开关 | 预期 marker | Timeout | 边界 |
| --- | --- | --- | ---: | --- |
| `memory-self-test` | `--mm_self_test=y` | `BIGOS_MM_SELF_TEST_PASSED` | 10s | 早期 allocator 与 direct-map self-test。 |
| `timer-irq` | `--timer_smoke=y` | `BIGOS_TIMER_IRQ` | 10s | PIC/PIT IRQ0 marker 路径。 |
| `scheduler` | `--scheduler_smoke=y` | `BIGOS_SCHED_THREAD_B` | 10s | 协作式内核线程 context switch 路径。 |
| `scheduler-semantics` | `--scheduler_semantics_smoke=y` | `BIGOS_SCHED_SEMANTICS_PASSED` | 15s | Time slice 到期、preemption-disable 延迟与 guarded IRQ-return reschedule。 |
| `blocking-primitives` | `--blocking_smoke=y` | `BIGOS_BLOCKING_SMOKE_PASSED` | 15s | Synthetic TTY producer 加 wait queue wakeup 与 timeout sleep。 |
| `syscall` | `--syscall_smoke=y` | `BIGOS_SYSCALL_SMOKE_PASSED` | 10s | `int 0x80` 最小 syscall ABI 路径。 |
| `filesystem-read` | `--fs_smoke=y` | `BIGOS_FS_EXFAT_READ_PASSED` | 20s | ATA PIO 加只读 exFAT backend 上的 VFS open/read/release 路径。 |
| `first-user-program` | `--user_program_smoke=y` | `BIGOS_USER_EXIT` | 20s | 以 lifecycle-core 进程运行 embedded flat image；smoke entry 仍默认关闭。 |
| `filesystem-user-elf` | `--user_elf_smoke=y` | `BIGOS_USER_EXIT` | 30s | 打包 `/boot/user/init.elf` 并通过可复用 ELF exec prepare 路径运行；smoke entry 仍默认关闭。 |
| `demand-paging` | `--demand_paging_smoke=y` | `BIGOS_DEMAND_PAGING_PASSED` | 30s | VMA-backed lazy anonymous 物化和确定性 fault 处理。 |
| `fork-cow` | `--fork_cow_smoke=y` | `BIGOS_FORK_COW_PASSED` | 30s | Bounded `fork` 与 anonymous COW split 语义。 |
| `time-identity` | `--time_identity_smoke=y` | `BIGOS_TIME_IDENTITY_PASSED` | 20s | 墙钟和 pid/ppid/uid/gid syscall 路径。 |
| `signals` | `--signal_smoke=y` | `BIGOS_SIGNAL_PASSED` | 30s | 最小 signal queue、mask、handler 和投递路径。 |
| `writable-fs` | `--writable_fs_smoke=y` | `BIGOS_WRITABLE_FS_PASSED` | 30s | RAM-backed `/rw`、page/buffer cache、写后读回、fsync 和权限。 |
| `pipe` | `--pipe_smoke=y` | `BIGOS_PIPE_PASSED` | 30s | Pipe/dup 端点计数、阻塞唤醒、EOF 和 `EPIPE`。 |
| `userland-runtime` | `--userland_smoke=y` | `BIGOS_USERLAND_PASSED` | 40s | crt0/libc wrapper、参数/环境传递、stdout/stderr、errno、简单 C 程序基线探针、shell 执行、fork/exec/wait、pipe、重定向和 malloc。 |
| `default-init` | _(无)_ | `BIGOS_USER_EXEC` | 40s | 不加任何 smoke 开关的默认构建；normal boot 打包 PID-1 init、`/bin/sh` 和 bounded `/bin/*`。 |

每个 case 只启用表中列出的 smoke 开关，并在构建前显式关闭其他 smoke 开关。runner 之外，所有 runtime smoke 选项仍保持默认关闭，除非开发者通过 `xmake f ...=y` 显式配置。

`default-init` 是不依赖任何 smoke 开关的行为断言 case：它以默认配置（所有 smoke 选项设为 `=n`）构建，并断言 normal boot 到达常驻 PID-1 init 和 `/bin/sh`，以 `BIGOS_USER_EXEC` 作为 QEMU headless marker。缺失该 marker 即判定为失败，不会被重新解读为通过。

交互控制台可用性验证叠加在该 case 之上。自动化 QEMU headless run 继续使用
serial/log marker 断言，不要求图形 display、手工键盘输入或 emulator scancode
injection。若 graphical QEMU、Bochs、手工键盘输入或 input injection 可用，validation
notes 应记录 backend、display/input method、输入命令、观察到的 prompt/echo/output
和结果。若这些能力不可用，需要将交互部分标记为 skipped 或 blocked，并记录替代的
source-level、build、headless 检查以及剩余 console-usability 风险。

`blocking-primitives` case 在最终 pass marker 前还会输出 `BIGOS_BLOCKING_WAIT_BLOCKED`、`BIGOS_BLOCKING_WAKE_SENT`、`BIGOS_BLOCKING_WAIT_RESUMED`、`BIGOS_BLOCKING_TIMEOUT_BLOCKED` 与 `BIGOS_BLOCKING_TIMEOUT_EXPIRED` 中间 marker。它使用 synthetic TTY producer，因此 QEMU headless 自动验证不依赖手工键盘输入；若执行可选手工键盘验证，需要单独记录。

`scheduler-semantics` case 在最终 pass marker 前还会输出 `BIGOS_SCHED_SEMANTICS_START`、`BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED` 与 `BIGOS_SCHED_SEMANTICS_PREEMPTED` 中间 marker。它验证 time-slice expiry 与 timer-driven IRQ-return reschedule，不会启用 memory、filesystem、user-program、user-ELF 或 broad smoke 选项。由于该 case 涉及 IRQ/timer/context-switch 行为，validation notes 需要记录 QEMU headless 串口日志，以及 Bochs 或 QEMU+Bochs 交叉验证是执行还是跳过。

process lifecycle core 现在会在 normal build 中编译。用户程序 smoke case 验证默认关闭的
entry thread 与 marker 行为；source-level checks 覆盖 PID 唯一性、有界进程表容量失败、
parent/child 链接、zombie-to-reap、wait wakeup、exec rollback、bounded `argv`/`envp`、
active-root teardown rejection 和 current-stack release deferral。

fd/VFS 壳层通过 source-level checks 加 `filesystem-read`、`filesystem-user-elf`、
`writable-fs`、`pipe` 和 `userland-runtime` runtime case 验证。只读 exFAT 路径仍是
boot/image 的 source of truth，而 `/rw` 与 pipe 语义是 bounded runtime 能力。fd/VFS
syscall 使用 DPL=3 `int 0x80` trap gate，并且必须在同步 storage I/O 或阻塞 pipe 操作前通过 `sched::can_block()`。

简单 C 程序验证分层接入默认关闭的 `userland-runtime` case。启用
`userland_smoke` 时，构建会通过与 `/bin/sh`、`/bin/echo`、`/bin/cat` 相同的
`crt0 + libc + -nostdlib -static` ELF64 路径，打包有界 `/bin/smoke/args`、
`/bin/smoke/env`、`/bin/smoke/out`、`/bin/smoke/errno` 和 `/bin/smoke/exit` 程序。
这些探针不会打包进普通镜像。smoke 会观察程序 stdout/stderr，检查参数与环境报告，
检查失败 wrapper 的 `errno` 翻译，观察请求的退出码探针，并向 `/bin/sh` 输入确定性命令脚本，
确认 shell 在外部程序非零退出后继续运行。该验证不新增 kernel syscall、不修改 `int 0x80` ABI、
不改变 boot/disk 布局，也不会让 emulator-dependent smoke 成为默认构建的强制依赖。

## 手工单 Case 流程

调试单个失败时仍可使用手工验证。需要在 review notes 或生成 artifact 中记录命令、smoke 开关、预期 marker、串口日志、结果、跳过的矩阵 case、替代检查和剩余风险。

示例：

```bash
xmake f --mm_self_test=y
uv run python tools/boot_debug.py run \
  --emulator qemu \
  --display none \
  --serial-log build/test/runtime-smoke/memory-self-test.serial.log \
  --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED \
  --smoke-timeout 10
```

## Artifact 字段

除非提供 `--output`，runner 会将 Markdown-first validation artifact 写入 `build/test/runtime-smoke-validation.md`。该 artifact 保留 JSON schema 兼容字段，便于未来自动化消费：

- `schema_version`：runtime smoke validation schema version。
- `tool availability`：`uv`、`xmake`、`x86_64-elf-*`、QEMU，以及可选 Bochs。
- `case id`：稳定的矩阵 case 标识。
- `xmake configuration`：case 使用的显式 smoke 开关。
- `expected marker` 与 `observed marker`：COM1 marker 对比。
- `blocking markers`：blocking case 的串口日志中出现的 wait/wake/timeout 中间 marker。
- `scheduler semantics markers`：scheduler semantics case 的 delayed-preemption 与 IRQ-return-preempted 中间 marker。
- `serial log path`：作为 source of truth 的生成日志。
- `timeout` 与 `exit status`：有界等待和失败上下文。
- `status`：`passed`、`failed`、`skipped` 或 `blocked`。
- `failed stage`：preflight、build、image build、validation 或 emulator marker 阶段。
- `skip reason`、`alternative checks` 与 `residual risk`：工具不可用或跳过交叉验证时必须记录。

缺少 `uv`、`xmake`、cross-binutils、QEMU、Bochs、ROM/display 配置或其他必要本地依赖时，必须记录为 skipped 或 blocked。未运行的 smoke 不得标记为 passed。

## 交叉验证

QEMU headless 是矩阵首选自动化 serial-marker 路径。涉及 boot、real-mode/protected-mode/long-mode transition、interrupt dispatch、timer IRQ、keyboard IRQ、ATA PIO、port IO 或低层 driver 行为的变更，仍应按场景在可用时执行 Bochs 或 QEMU+Bochs 交叉验证。

如果 Bochs 交叉验证不可用，需要记录跳过原因、使用了哪些 QEMU、build、source-level 或手工替代检查，以及剩余 hardware-behavior 风险。

## 保持不变的契约

runtime smoke 产品化不得改变 kernel link address、BootInfo 或 handoff ABI、page-table 假设、IDT vector、IRQ EOI 规则、syscall vector `0x80`、CR3 切换规则、smoke marker 字符串或默认关闭的 smoke entry 边界。镜像路径仍是现有 Legacy BIOS raw image，包含 MBR/exFAT、`/boot/boot.bin`、根目录 `kernel` 和 IDE-compatible disk exposure；不要求 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe 或新 storage driver。
