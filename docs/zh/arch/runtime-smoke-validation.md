# 运行时 Smoke 验证

BigOS 将现有默认关闭的 runtime smoke 产品化为一组面向当前有界基线的窄验证矩阵。该矩阵只属于 tooling 和文档层：不新增内核运行时能力、不接入 CI、不新增默认 UEFI 启动路径之外的 firmware 能力、不新增存储驱动，也不改变 smoke marker ABI。

## 矩阵 Runner

- 首选自动化命令：`uv run python -m tools.bigosdev smoke matrix`
- 单 case 命令：`uv run python -m tools.bigosdev smoke matrix --case memory-self-test`
- Artifact 覆盖路径：`uv run python -m tools.bigosdev smoke matrix --output logs/runtime-smoke-validation.md`
- 串口日志：默认每个 case 一个文件，位于 `logs/runtime-smoke/`。
- Image：默认每个 case 一个 UEFI ESP/FAT image 和一个 exFAT 兼容 root image，位于 `build/test/runtime-smoke/`。

显式传入的 runtime smoke 串口日志目录也必须解析到 `logs/` 下。runner 会拒绝
`build/test/`、`log/` 或其它非 `logs/` 目录下的 `--serial-log-dir`。`build/test/`
只用于生成 image 和 emulator 配置文件，不用于串口日志。

runner 会通过 `xmake f` 显式配置每个 case，经由现有 xmake-backed flow 构建，准备默认 UEFI ESP/FAT image 和当前 exFAT 兼容 root image，使用 `--display none` 启动 QEMU/OVMF，并在 case-specific timeout 内等待预期 COM1 marker。

## 验证入口盘点

行为导向验证区分三类入口：

- 默认路径：`default-init` case 使用所有 smoke 开关关闭的 normal build，打包 PID-1 init、`/bin/sh` 和有界 `/bin/*`，并通过 QEMU headless 日志观察确定性的 init/shell 串口 marker。
- 默认关闭 smoke：`userland-runtime`、`filesystem-maturity`、`writable-fs`、`pipe`、`filesystem-read`、`filesystem-user-elf` 等 case 每次只启用一个显式 smoke 开关，用于验证 userland、process/fd、pipe/redirection 和 filesystem 行为，且不改变 normal boot 默认值。
- 场景化证据：graphical QEMU、Bochs、手工键盘输入、emulator input injection 或硬件相邻检查只在为 console usability 或底层 boot/IRQ/timer/ATA/port-IO 风险补充证据时记录。

## 矩阵 Case

| Case | xmake 开关 | 预期 marker | Timeout | 边界 |
| --- | --- | --- | ---: | --- |
| `memory-self-test` | `--mm_self_test=y` | `BIGOS_MM_SELF_TEST_PASSED` | 10s | 早期 allocator 与 direct-map self-test。 |
| `timer-irq` | `--timer_smoke=y` | `BIGOS_TIMER_IRQ` | 10s | PIC/PIT IRQ0 marker 路径。 |
| `scheduler` | `--scheduler_smoke=y` | `BIGOS_SCHED_THREAD_B` | 10s | 协作式内核线程 context switch 路径。 |
| `scheduler-semantics` | `--scheduler_semantics_smoke=y` | `BIGOS_SCHED_SEMANTICS_PASSED` | 15s | Time slice 到期、preemption-disable 延迟与 guarded IRQ-return reschedule。 |
| `scheduler-smp` | `--scheduler_smp_smoke=y` | `BIGOS_SCHED_SMP_PASSED` | 20s | Per-CPU run queue 上的一个 BSP worker 和一个 AP-placed worker；不声明 generic IPI、TLB shootdown、CPU hotplug 或完整 APIC interrupt migration。 |
| `apic-default-interrupt-delivery` | `--scheduler_smp_smoke=y` | `BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE` | 20s | LAPIC timer ownership、IOAPIC keyboard 到 BSP 的路由、APIC EOI ownership 与 bounded SMP startup gate；不声明 CPU hotplug、NUMA、MSI/MSI-X、完整 IRQ affinity 或 backend parity。 |
| `multicore-hardening` | `--multicore_hardening_smoke=y` | `BIGOS_MULTICORE_HARDENING_PASSED` | 25s | 默认关闭的 QEMU legacy 2 CPU hardening 路径，覆盖 AP scheduler placement、scheduler nudge IPI、remote wakeup、timeout wakeup、TLB shootdown completion 和普通 userland baseline marker；不声明 CPU hotplug、NUMA、async I/O、完整 POSIX concurrency 或 UEFI runtime parity。 |
| `blocking-primitives` | `--blocking_smoke=y` | `BIGOS_BLOCKING_SMOKE_PASSED` | 15s | Synthetic TTY producer 加 wait queue wakeup 与 timeout sleep。 |
| `syscall` | `--syscall_smoke=y` | `BIGOS_SYSCALL_SMOKE_PASSED` | 10s | `int 0x80` 最小 syscall ABI 路径。 |
| `filesystem-read` | `--fs_smoke=y` | `BIGOS_FS_EXFAT_READ_PASSED` | 20s | ATA PIO 加只读 exFAT backend 上的 VFS open/read/release 路径。 |
| `block-io-request-layer` | `--block_io_request_smoke=y` | `BIGOS_BLOCK_IO_REQUEST_PASSED` | 20s | 有界同步请求提交、校验失败、单设备队列耗尽、unsupported 写入、设备错误传播和内核内部 interrupt-driven completion；不声明完整 async I/O 或用户 ABI。 |
| `bounded-network-protocol` | `--network_protocol_smoke=y` | `BIGOS_NETWORK_PROTOCOL_PASSED` | 20s | 内核内部有界 Ethernet II、ARP、IPv4、ICMP echo 与 UDP datagram 路径；不声明 socket、fd、syscall、设备节点、DHCP、DNS、TCP、IPv6 或完整网络栈。 |
| `minimal-socket-interface` | `--socket_smoke=y` | `BIGOS_SOCKET_PASSED` | 20s | 内核内部闭环验证最小用户可见 UDP socket fd backend 与 bind/sendto/recvfrom 路径（注入式 frame）：socket fd 创建、ops 身份判别、不支持的 `read`/`write`、bind、send、receive、无数据与超长错误路径，以及 close 时 endpoint 的 exactly-once 回收。仅有界 UDP 子集；不声明完整 POSIX socket API、TCP/stream、`connect`/`listen`/`accept`、选项矩阵、DHCP、DNS、IPv6 或真实 tap/网络后端，且 recvfrom 为有界（非通用 POSIX 阻塞）。 |
| `modern-storage-backend` | `--modern_storage_backend_smoke=y` | `BIGOS_MODERN_STORAGE_BACKEND_PASSED` | 30s | 显式选择 modern virtio-blk backend，经 request layer、cache 写读往返和写回失败保留 dirty state；默认 boot 仍为 ATA-backed，不声明 crash consistency、power-loss recovery、设备节点或 async I/O。 |
| `first-user-program` | `--user_program_smoke=y` | `BIGOS_USER_EXIT` | 20s | 以 lifecycle-core 进程运行 embedded flat image；smoke entry 仍默认关闭。 |
| `filesystem-user-elf` | `--user_elf_smoke=y` | `BIGOS_USER_EXIT` | 30s | 打包 `/boot/user/init.elf` 并通过可复用 ELF exec prepare 路径运行；smoke entry 仍默认关闭。 |
| `demand-paging` | `--demand_paging_smoke=y` | `BIGOS_DEMAND_PAGING_PASSED` | 30s | VMA-backed lazy anonymous 物化和确定性 fault 处理。 |
| `fork-cow` | `--fork_cow_smoke=y` | `BIGOS_FORK_COW_PASSED` | 30s | Bounded `fork` 与 anonymous COW split 语义。 |
| `time-identity` | `--time_identity_smoke=y` | `BIGOS_TIME_IDENTITY_PASSED` | 20s | 墙钟和 pid/ppid/uid/gid syscall 路径。 |
| `signals` | `--signal_smoke=y` | `BIGOS_SIGNAL_PASSED` | 30s | 最小 signal queue、mask、handler 和投递路径。 |
| `writable-fs` | `--writable_fs_smoke=y` | `BIGOS_WRITABLE_FS_PASSED` | 30s | RAM-backed `/rw`、page/buffer cache、append/cross-block/seek-past-EOF 增长、zero gap、truncate、块复用、fsync 和权限。 |
| `persistent-writable-fs-write` | `--persistent_writable_fs_smoke=y` | `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED` | 40s | 带 `--persistent-image` 的第一次 boot：显式格式化独立测试磁盘、有界增长/截断、`fsync` 与缓存淘汰后读回。 |
| `persistent-writable-fs-verify` | `--persistent_writable_fs_smoke=y` | `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED` | 40s | 复用同一 `--persistent-image` 的第二次 boot：mount-existing 并在 clean reboot 后读回已同步 `/rw` 增长/截断状态。 |
| `persistent-writable-fs-modern` | `--persistent_writable_fs_smoke=y --persistent_writable_fs_modern_backend=y` | `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED` / `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED` | 40s | emulator 提供 modern device image 时，在显式 modern virtio-blk validation role 上运行同一有界 clean-sync persistent `/rw` 流程；不声明默认存储替换或 power-loss recovery。 |
| `pipe` | `--pipe_smoke=y` | `BIGOS_PIPE_PASSED` | 30s | Pipe/dup 端点计数、阻塞唤醒、EOF 和 `EPIPE`。 |
| `filesystem-maturity` | `--filesystem_maturity_smoke=y` | `BIGOS_FILESYSTEM_MATURITY_PASSED` | 40s | runtime filesystem maturity 当前运行期文件系统语义，覆盖只读 exFAT、RAM-backed `/rw`、fd/VFS、metadata、cwd-relative path、libc errno 与 shell-visible tools；不声明重启持久化。 |
| `userland-runtime` | `--userland_smoke=y` | `BIGOS_USERLAND_PASSED` | 40s | crt0/libc wrapper、参数/环境传递、stdout/stderr、errno/error text、ctype、有界 time/assert、`snprintf`/formatter、`strtol`/`strtoul`/`atoi`、无隐藏状态 search helper、`calloc`/`realloc`、有界 `DIR*` wrapper、简单 C 程序基线探针、shell 执行、fork/exec/wait、pipe、重定向和有界 `/rw` 运行时文件操作。 |
| `libc-file-stream` | `--libc_file_stream_smoke=y` | `BIGOS_LIBC_FILE_STREAM_PASSED` | 40s | 有界缓冲 `FILE` 流子集：`fopen`/`freopen`/`fclose`、缓冲 `fread`/`fwrite` 回环、`fgets`、`fseek`/`ftell`/`rewind`、`setvbuf` 模式与调用方缓冲、`fflush`/`feof`/`ferror`/`clearerr`、退出路径刷新、`freopen(stdout)` 重定向、确定性失败路径，以及扩充的 `string`/`stdlib`/`ctype` helper。 |
| `default-init` | _(无)_ | `BIGOS_USER_EXEC` | 40s | 不加任何 smoke 开关的默认构建；normal boot 打包 PID-1 init、`/bin/sh` 和 bounded `/bin/*`。 |

每个 case 只启用表中列出的 smoke 开关，并在构建前显式关闭其他 smoke 开关。runner 之外，所有 runtime smoke 选项仍保持默认关闭，除非开发者通过 `xmake f ...=y` 显式配置。

## 行为导向矩阵

当前有界最小可用系统基线将 runtime 矩阵从仅 marker 的 smoke 覆盖推进为有界最小可用系统的行为断言。每一行记录被验证的 capability、确定性输入、预期可观察结果、失败信号、验证层和环境依赖。这些检查的默认 backend 现在是 x86_64 UEFI QEMU/OVMF 路径。Runtime parity 仍限定在当前 init/shell/user-program baseline，不暗示 Secure Boot、超出单独 console backend 记录的图形 framebuffer 证据、ACPI handoff、Runtime Services、超出被测路径的 OVMF parity、virtio、AHCI/SATA、NVMe、新存储驱动、动态链接、作业控制、完整 shell grammar 或完整 POSIX libc。

| Capability | 输入或路径 | 预期可观察结果 | 失败信号 | 验证层 | 环境依赖 |
| --- | --- | --- | --- | --- | --- |
| 默认 init 与 `/bin/sh` 可达性 | `default-init` normal boot，无 smoke 开关 | PID-1 init 启动并 launch 常驻 `/bin/sh`，通过 `BIGOS_INIT_ENTER` 后出现 `BIGOS_USER_EXEC` 观察 | 缺失预期 marker、timeout、emulator 退出或 panic marker | QEMU/OVMF headless runtime 断言 | xmake、cross-binutils、LLVM/LLD、mtools、QEMU、OVMF、串口日志、UEFI ESP/FAT image、exFAT 兼容 root image |
| 简单 C 参数/环境/stdout/stderr | `userland-runtime` 运行 `/bin/smoke/args`、`/bin/smoke/env` 和 `/bin/smoke/out` | `/rw` 记录预期 `argc`/`argv`、确定性 environment 边界文本和 stdout/stderr transcript 内容 | `BIGOS_USERLAND_FAILED <reason>`、缺失 `/rw` 记录、错误 exit status 或缺失 pass marker | 默认关闭 userland runtime 断言 | xmake、cross-binutils、QEMU、串口日志、RAM-backed `/rw` |
| 简单 C `errno` 与退出状态 | `userland-runtime` 运行失败 open/exec wrapper 和 `/bin/smoke/exit 7` | 错误 wrapper 报告文档化 `errno`，失败 `execve` 后 caller 存活，parent 观察到请求的 child status | failure marker、status 不匹配、`errno` 错误或缺失 continuation 输出 | 默认关闭 userland runtime 断言 | xmake、cross-binutils、QEMU、串口日志 |
| Shell continuation 与 unsupported syntax | 非交互 `/bin/sh` 脚本运行非零程序、unsupported pipe syntax，再运行 `echo shell-alive` | shell 输出确定性 syntax/error 文本并继续执行下一条命令 | 缺失错误文本、缺失 `shell-alive`、shell 崩溃或缺失 pass marker | 默认关闭 userland runtime 断言；可选手工交互证据 | QEMU headless 执行脚本断言；display/input 仅用于可选交互记录 |
| `exec`/`wait` 与 fd 继承 | userland smoke fork、exec packaged program、wait，并保持 stdio 或 redirected descriptor | child output 或 `/rw` 记录可见，parent wait 返回预期 child/status，继承 descriptor 仍可用 | wait 失败、status 错误、缺失输出、descriptor 失败或 failure marker | 默认关闭 userland runtime 断言 | xmake、cross-binutils、QEMU、串口日志 |
| `dup`、redirection 与无关 fd 状态 | userland smoke 复制 fd、关闭原 fd、将 shell 输出重定向到 `/rw` 并读回 | duplicate fd 在原 fd 关闭后仍可写；重定向文件包含预期数据；shell transcript 仍可用 | 文件内容错误、dup/readback 失败、shell fd 损坏或 failure marker | 默认关闭 userland runtime 断言 | QEMU headless、串口日志、RAM-backed `/rw` |
| Pipe 端点行为 | userland smoke 传输 `pipe-data`；shell 运行 `echo pipe-ok | /bin/cat`；`pipe` smoke 检查端点计数 | downstream reader 看到字节，writer 全关后出现 EOF，无关 fd 仍可用 | 数据错误、缺失 EOF、`EPIPE`/端点状态不匹配、缺失 pass marker 或 panic | 默认关闭 userland runtime 断言加窄 pipe smoke | QEMU headless、串口日志 |
| 有界网络协议路径 | `bounded-network-protocol` smoke、静态 IPv4 context、受控 Ethernet frame | ARP cache/reply、IPv4 校验、ICMP echo reply、UDP bind/send/receive 和 unsupported-frame 拒绝完成，并输出 `BIGOS_NETWORK_PROTOCOL_PASSED` | 缺失 marker、协议 failure marker、malformed packet 被接受、队列/容量行为不匹配或意外暴露用户 ABI | 默认关闭内核内部协议断言 | xmake、cross-binutils、QEMU 串口日志；仅在选择 packet-injection 证据时要求 TAP/virtio-net host backend |
| 最小 UDP socket 接口 | `minimal-socket-interface` smoke、静态 IPv4 context、socket fd backend 加注入式 datagram | socket fd 创建、ops 身份判别、不支持的 `read`/`write`、bind、sendto、recvfrom（来源地址/payload 正确）、无数据（映射 `EAGAIN`）与超长（映射 `EMSGSIZE`）错误路径、以及 close 时 endpoint 的 exactly-once 回收完成，并输出 `BIGOS_SOCKET_PASSED` | 缺失 marker、socket failure marker、payload/来源错误、endpoint 泄漏或双重释放，或意外阻塞行为 | 默认关闭内核内部 socket 断言 | xmake、cross-binutils、QEMU 串口日志；不要求真实 tap/网络后端 |
| 运行时 `/rw` 文件系统操作 | userland/filesystem maturity smoke 创建文件/目录、write、fsync、seek、truncate、读回、枚举 `/rw`、rename、unlink，并检查只读 backend 拒写 | 文件内容、metadata、zero gap、截断 size、目录项、稳定后端顺序、open-fd 生命周期和 `EROFS`/`ENOENT`/`EEXIST`/`ENOSPC`/`ERANGE` 错误符合有界 VFS 契约 | 内容错误、gap 泄漏旧字节、缺失 dirent、意外持久化要求、`errno` 错误或 failure marker | 默认关闭 filesystem maturity/userland 断言加 writable-fs smoke | QEMU headless、串口日志、RAM-backed `/rw` |
| 底层 boot/IRQ/timer/storage 行为 | 窄 memory/timer/scheduler/blocking/filesystem case；相关时使用 Bochs 或 QEMU+Bochs | 预期 marker 与可选中间 marker 出现；可用时记录跨 emulator 结果 | 缺失 marker、panic、timeout，或跳过 cross-validation 但无风险记录 | source/spec、build、QEMU headless、可选 Bochs/graphical 证据 | toolchain 加所选 emulator；Bochs/display/ROM 仅在场景需要时要求 |

`default-init` 是不依赖任何 smoke 开关的行为断言 case：它以默认配置（所有 smoke 选项设为 `=n`）构建，并断言 normal boot 到达常驻 PID-1 init 和 `/bin/sh`，以 `BIGOS_USER_EXEC` 作为 QEMU headless marker。缺失该 marker 即判定为失败，不会被重新解读为通过。

交互控制台可用性验证叠加在该 case 之上。自动化 QEMU headless run 继续使用
serial/log marker 断言，不要求图形 display、手工键盘输入或 emulator scancode
injection。若 graphical QEMU、Bochs、手工键盘输入或 input injection 可用，validation
notes 应记录 backend、display/input method、输入命令、观察到的 prompt/echo/output
、EOF-like input、interrupt-like line cancellation、unsupported-control behavior
和结果。若这些能力不可用，需要将交互部分标记为 skipped 或 blocked，并记录替代的
source-level、build、headless 检查以及剩余 console-usability 风险。

framebuffer console 验证与默认串口 marker 通过条件分开记录。若 QEMU + OVMF 图形证据可用，notes
应记录 framebuffer geometry、`BIGOS_CONSOLE_RENDER backend=framebuffer-text`、计算出的可见 columns/rows、full framebuffer background clear 结果、可见文本、软件光标行为，以及 PageUp/PageDown/Home/End viewport 重绘行为。Unicode console 验证还应在执行时记录有界 UTF-8/CJK 样例输出、双宽 cell 布局和 Legacy VGA 固定 80x25 降级。缺少 OVMF、QEMU、display/screenshot 支持、framebuffer metadata 或 glyph lookup readiness 时，应标记为 skipped 或 blocked，并记录 VGA fallback、source-level checks、构建结果和剩余图形 console 风险。framebuffer Unicode console 验证通过也不声明 ANSI/VT、`termios`、多终端、locale、shaping、输入法或完整 POSIX terminal。

`blocking-primitives` case 在最终 pass marker 前还会输出 `BIGOS_BLOCKING_WAIT_BLOCKED`、`BIGOS_BLOCKING_WAKE_SENT`、`BIGOS_BLOCKING_WAIT_RESUMED`、`BIGOS_BLOCKING_TIMEOUT_BLOCKED` 与 `BIGOS_BLOCKING_TIMEOUT_EXPIRED` 中间 marker。它使用 synthetic TTY producer，因此 QEMU headless 自动验证不依赖手工键盘输入；若执行可选手工键盘验证，需要单独记录。

`scheduler-semantics` case 在最终 pass marker 前还会输出 `BIGOS_SCHED_SEMANTICS_START`、`BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED` 与 `BIGOS_SCHED_SEMANTICS_PREEMPTED` 中间 marker。它验证 time-slice expiry 与 timer-driven IRQ-return reschedule，不会启用 memory、filesystem、user-program、user-ELF 或 broad smoke 选项。由于该 case 涉及 IRQ/timer/context-switch 行为，validation notes 需要记录 QEMU headless 串口日志，以及 Bochs 或 QEMU+Bochs 交叉验证是执行还是跳过。

`scheduler-smp` case 会在最终 pass marker 前输出 `BIGOS_AP_ONLINE`、`BIGOS_AP_LOCAL_TIMER`、`BIGOS_SCHED_SMP_BSP_THREAD` 与 `BIGOS_SCHED_SMP_AP_THREAD`。矩阵为该 case 注入 QEMU `-cpu max -smp 2`；若 QEMU、APIC、cross-binutils、Bochs 多核支持、display 或 ROM 配置不可用，验证记录必须明确 skipped 或 blocked 证据和残余风险。

process lifecycle core 现在会在 normal build 中编译。用户程序 smoke case 验证默认关闭的
entry thread 与 marker 行为；source-level checks 覆盖 PID 唯一性、有界进程表容量失败、
parent/child 链接、zombie-to-reap、wait wakeup、exec rollback、bounded `argv`/`envp`、
active-root teardown rejection 和 current-stack release deferral。

fd/VFS 壳层通过 source-level checks 加 `filesystem-read`、`filesystem-user-elf`、
`writable-fs`、`pipe`、`filesystem-maturity` 和 `userland-runtime` runtime case 验证。只读 exFAT 路径仍是
boot/image 的 source of truth，而 `/rw` 与 pipe 语义是 bounded runtime 能力。RAM-backed
`/rw` 只保证当前运行期一致性，不跨重启持久化，也不改变 Legacy BIOS/MBR/exFAT 磁盘镜像。
Persistent `/rw` 验证使用独立测试磁盘，只声明 clean-sync 加 clean-reboot 后可见；若
QEMU/Bochs 对额外磁盘支持不可用，需要记录为 skipped 或 blocked，并写明剩余 storage 风险。
fd/VFS syscall 使用 DPL=3 `int 0x80` trap gate，并且必须在同步 storage I/O 或阻塞 pipe 操作前通过 `sched::can_block()`。

简单 C 程序验证分层接入默认关闭的 `userland-runtime` case。启用
`userland_smoke` 时，构建会通过与 `/bin/sh`、`/bin/echo`、`/bin/cat` 相同的
`crt0 + libc + -nostdlib -static` ELF64 路径，打包有界 `/bin/smoke/args`、
`/bin/smoke/env`、`/bin/smoke/out`、`/bin/smoke/errno`、`/bin/smoke/exit` 和
`/bin/smoke/libc_subset` 程序。这些探针不会打包进普通镜像。smoke 会观察程序
stdout/stderr，检查参数与环境报告，检查失败 wrapper 的 `errno` 翻译，观察请求的退出码探针，
通过 ctype、time、assert、无符号转换、无隐藏状态 search、formatter/error text 和目录
wrapper 断言覆盖 portable libc subset，并向 `/bin/sh` 输入确定性命令脚本，确认 shell
在外部程序非零退出后继续运行。该验证不新增 kernel syscall、不修改 `int 0x80` ABI、
不改变 boot/disk 布局，也不会让 emulator-dependent smoke 成为默认构建的强制依赖。

## 手工单 Case 流程

调试单个失败时仍可使用手工验证。需要在 review notes 或生成 artifact 中记录命令、smoke 开关、预期 marker、串口日志、结果、跳过的矩阵 case、替代检查和剩余风险。

示例：

```bash
xmake f --mm_self_test=y
uv run python -m tools.bigosdev run \
  --emulator qemu \
  --display none \
  --serial-log logs/runtime-smoke/memory-self-test.serial.log \
  --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED \
  --smoke-timeout 10
```

## Artifact 字段

除非提供 `--output`，runner 会将 Markdown-first validation artifact 写入 `logs/runtime-smoke-validation.md`。该 artifact 保留 JSON schema 兼容字段，便于未来自动化消费：

- `schema_version`：runtime smoke validation schema version。
- `tool availability`：`uv`、`xmake`、`x86_64-elf-*`、QEMU，以及可选 Bochs。
- `case id`：稳定的矩阵 case 标识。
- `xmake configuration`：case 使用的显式 smoke 开关。
- `expected marker` 与 `observed marker`：COM1 marker 对比。
- `blocking markers`：blocking case 的串口日志中出现的 wait/wake/timeout 中间 marker。
- `scheduler semantics markers`：scheduler semantics case 的 delayed-preemption 与 IRQ-return-preempted 中间 marker。
- `serial log path`：作为 source of truth 的生成日志。
- `modern storage device configuration`：现代存储 case 请求的精确 QEMU device/drive 参数。
- `boot image category`：记录验证仍使用现有 UEFI ESP/FAT 加 exFAT root image，或在 legacy case 中使用 Legacy BIOS/MBR/exFAT raw image。
- `modern storage stage results`：分开记录 backend publication、request completion、cache/writeback round trip、default boot regression 和 cross-validation。
- `timeout` 与 `exit status`：有界等待和失败上下文。
- `status`：`passed`、`failed`、`skipped` 或 `blocked`。
- `failed stage`：preflight、build、image build、validation 或 emulator marker 阶段。
- `skip reason`、`alternative checks` 与 `residual risk`：工具不可用或跳过交叉验证时必须记录。
- `console backend evidence`：可选记录 framebuffer/VGA backend selection、framebuffer geometry、计算出的可见 columns/rows、full-clear observation、可见文本/光标/scrollback notes，以及图形证据 skipped 或 blocked 状态。

缺少 `uv`、`xmake`、cross-binutils、QEMU、Bochs、ROM/display 配置、serial capture、disk image generation、现代存储 device model 支持或其他必要本地依赖时，必须记录为 skipped 或 blocked。未运行的 smoke 不得标记为 passed。现代存储验证必须单独记录 `default-init` 回归；如果未选择或未运行该回归，artifact 将其记录为 skipped，而不是合并到 modern backend 结果里。

## 默认 UEFI Smoke

x86_64 UEFI boot backend 是默认 smoke 入口。可运行
`xmake run qemu -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`，
或直接使用 helper：
`uv run python -m tools.bigosdev run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log logs/qemu-uefi.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`。`xmake run qemu-uefi` 仍是同一 backend 的显式别名。

UEFI smoke 会构建/使用 `BOOTX64.EFI`，创建包含 kernel、PID-1 init、`/bin/sh`、有界
`/bin/*` 和 `/boot/fonts/unifont.bin` 的 ESP/FAT image，并为当前 VFS baseline 准备 exFAT 兼容 root image，使用 x86_64 OVMF 启动 QEMU，并默认使用
`logs/qemu-uefi.serial.log`。它要求 QEMU/OVMF、Homebrew LLVM/LLD、`mtools`、
现有 x86_64 cross toolchain，以及用于 Python helper validation 的 `uv`。缺少 OVMF、
mtools、LLVM/LLD、QEMU、cross toolchain 或 `uv` 时，必须记录为 skipped 或 blocked，
并写明替代检查和剩余 UEFI bootability 风险。

UEFI 默认 runtime marker 与此前 Legacy BIOS headless 路径使用的默认 init/user exec marker 相同，当前为 `BIGOS_USER_EXEC`。
Framebuffer handoff 证据应与通过条件分开记录，可使用 `BIGOS_UEFI_FRAMEBUFFER` 等 serial
诊断，以及确认 framebuffer 物理范围已从 ordinary RAM 和 direct-map 初始化中排除的 kernel/source-level
检查。Glyph lookup font asset readiness 也应与通过条件分开记录：validation notes 应说明
`build/assets/fonts/unifont.bin` 是否已生成为 glyph lookup payload、是否打包到 ESP
`/boot/fonts/unifont.bin`、UEFI backend 是否通过 `BIGOS_UEFI_FONT` 加载，以及 kernel-side
validation 是否通过 `BIGOS_FONT_LOOKUP ready` 接受或以显式 unavailable stage 拒绝。framebuffer
metadata、font metadata 或 glyph lookup validation 缺失属于 handoff/lookup fallback；只要达到 bounded
userland marker，它不是默认 UEFI boot failure。缺失 `BIGOS_USER_EXEC` 是 failed 或 blocked UEFI runtime-parity
check，不是通过。Apple Silicon 主机可能通过 TCG 运行 x86_64 QEMU，因此 validation notes 应记录 timeout
和性能相关剩余风险。

Glyph lookup readiness 是有界 framebuffer console backend 的输入。它本身不证明 framebuffer glyph rendering、Unicode
display、software cursor support、framebuffer scrollback、Secure Boot、ACPI handoff、UEFI Runtime Services
或完整 device/storage parity；验证这些结论时必须单独记录选中的 console backend 和图形/手工证据。

## 交叉验证

QEMU headless 是矩阵首选自动化 serial-marker 路径。涉及 boot、real-mode/protected-mode/long-mode transition、interrupt dispatch、timer IRQ、keyboard IRQ、ATA PIO、port IO 或低层 driver 行为的变更，仍应按场景在可用时执行 Bochs 或 QEMU+Bochs 交叉验证。

如果 Bochs 交叉验证不可用，需要记录跳过原因、使用了哪些 QEMU、build、source-level 或手工替代检查，以及剩余 hardware-behavior 风险。

## 保持不变的契约

runtime smoke 产品化不得改变 kernel link address、BootInfo 或 handoff ABI、page-table 假设、IDT vector、IRQ EOI 规则、syscall vector `0x80`、CR3 切换规则、smoke marker 字符串或默认关闭的 smoke entry 边界。默认 UEFI 路径使用 ESP/FAT image 承载 `BOOTX64.EFI` 和 loader payload，并用 exFAT 兼容 root image 支撑当前 kernel VFS baseline；它不引入 FAT runtime filesystem、virtio、AHCI/SATA、NVMe 或新 storage driver。Legacy BIOS raw image 路径仍通过显式 Legacy backend 选择保留，包含 MBR/exFAT、`/boot/boot.bin`、根目录 `kernel` 和 IDE-compatible disk exposure；作为对比验证运行时应单独记录。
