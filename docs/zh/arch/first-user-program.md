# 首个用户程序运行路径

阶段 6 的 `user_program_smoke` 是默认关闭的验证路径，用来证明 BigOS 可以创建一个最小用户进程、
加载内嵌用户程序、进入 CPL3，并通过 `write` / `exit` syscall 回到内核。

阶段 8 新增独立的默认关闭 `user_elf_smoke` 路径。阶段 12 将共享进程运行时提升为
常规编译的 lifecycle core，同时保持两个 smoke entry 默认关闭。ELF smoke 复用
lifecycle core、syscall gate、用户 fault 路径、bounded `argv`/`envp` 栈布置和延后
reaper，但用户程序来自内核只读 exFAT 栈读取的 `/boot/user/init.elf`，而不是内嵌
flat blob。

## 镜像格式

首个用户程序采用内嵌 flat blob，而不是 ELF64 或文件系统加载：

- flat blob 由 `kernel/core/proc/proc.cc` 中的 `FIRST_USER_CODE` 字节序列提供，编入内核镜像。
- 镜像不依赖内核 FS、块设备、hosted OS 文件 IO 或 bootloader-only exFAT helper。
- blob 只执行 bounded `SYS_WRITE(fd=1, buf, len)`，随后执行 `SYS_EXIT(0)`。
- loader 仍显式映射 code、data/BSS 和 stack，便于验证权限边界；data/BSS 页当前为清零页。

ELF smoke 使用 `xmake build user-init-elf` 构建的静态 freestanding ELF64
`ET_EXEC` 镜像，并可由 `tools/boot_debug.py` 打包到 `/boot/user/init.elf`。
该 ELF 镜像通过 `SYS_WRITE` 输出 `BIGOS_USER_ELF_WRITE\n`，随后执行
`SYS_EXIT(0)`。

## 默认进入用户态 init

阶段 14.5 把 ring3 进入提升为 normal boot 的固定步骤。`kernel()` 在
`proc::init()` 与 `sched::start()` 之间创建一个默认开启、无 `#ifdef` 守卫的内核
线程运行 `bigos::proc::launch_init`。`launch_init` 复用只读 VFS/exFAT 路径：
`vfs::init` -> `open_absolute(INIT_ELF_PATH)` -> bounded 读入 ->
`create_elf_user_process` -> `run_user_process`。`INIT_ELF_PATH` 是语义中性常量，
指向 `/boot/user/init.elf`；现有 `USER_ELF_SMOKE_PATH` 保持不变并共用同一打包产物。
`user-init-elf` target 现在默认构建，因此 normal boot 始终打包可运行的 init 镜像；
`user_program_smoke` / `user_elf_smoke` 及其 `BIGOS_USER_*` marker 作为额外的
默认关闭验证路径被保留。

init marker 与 smoke `BIGOS_USER_*` marker 区分开：

- 内核在进入 ring3 前发出 `BIGOS_INIT_ENTER`。
- `init.elf` 缺失、超过 `USER_ELF_MAX_FILE_BYTES` 或非法时，发出
  `BIGOS_INIT_LOAD_FAILED <reason>` 后进入统一 panic 路径
  （`BIGOS_PANIC ... source=launch_init`）。这是有意的 PID-1 语义雏形，也是一个
  新的 normal-boot 失败模式。
- init 通过 `SYS_EXIT` 正常退出时，内核在共享的 `BIGOS_USER_EXIT` 之后发出
  `BIGOS_INIT_EXIT`，并落入现有延后 reaper 与 idle 调度，而非 panic。Stage 19 将
  one-shot init payload 替换为常驻 PID-1，由它启动并重启 `/bin/sh`；孤儿收养仍限定在当前进程模型内。

## 进程生命周期

`bigos::proc::Process` 是单核 bounded lifecycle 记录。即使
`user_program_smoke` 和 `user_elf_smoke` 都关闭，核心也会在 normal build 中编译；
这些开关只控制 smoke entry 线程和 user ELF artifact。每个进程记录：

- 稳定 PID、parent PID、child/sibling 链接、process-table 发布状态、用户页表根、进入前的 kernel CR3、用户入口、用户 code/data/stack 范围。
- 专用 syscall/exception kernel stack top，用于 TSS/RSP0。
- 生命周期状态 `Created` / `Running` / `Terminated` / `Faulted` / `Zombie` / `ReapPending` / `Reaped`、exit code、fault reason、owned 用户帧和 kernel stack 范围。
- wait status 消费状态和 safe reaper 元数据，确保 zombie 被父进程消费或按策略进入最终回收前不会复用 PID。

进程对象和当前 kernel stack 不在 `exit` 或 fault 返回路径立即释放；termination 只记录
status、唤醒可等待的 parent，并在执行已切换到非目标 kernel stack 且 CR3 root 不活跃后
交给 safe reaper。

## 加载与地址空间

loader 在非中断上下文运行：

- `derive_user_address_space_root()` 产生低半区清零、高半区共享的用户根。
- `map_page_in_root()` 将 code 映射为 `USER_CODE`，data/BSS/stack 映射为 `USER_DATA`。
- 加载失败输出 `BIGOS_USER_LOAD_FAILED` 并 halt，禁止进入部分初始化的 ring3。
- 仅 `proc::run_user_process()` 写 CR3 激活用户根；普通派生 helper 不隐式切换地址空间。

ELF loader 只接受 bounded x86_64 little-endian ELF64 `ET_EXEC`。它会拒绝不支持的
program header、W+X segment、重叠 segment、入口点不在 executable segment 内、越过
低半区用户窗口的范围，以及与 `USER_STACK_TOP` 处单页用户栈碰撞的范围。ELF 准备成功
输出 `BIGOS_USER_ELF_LOAD_PASSED`；bounded 加载失败输出
`BIGOS_USER_ELF_LOAD_FAILED <reason>`，且不会进入 ring3。general exec primitive 在
commit 前准备新 image，发布前失败会 rollback；若 commit 后旧 image 已无法恢复，则通过
确定性的 exec failure status 终止当前进程。

ELF 初始用户栈使用最小 libc-like 形状：`argc`、`argv[]`、`envp[]` 和 bounded 字符串，
由仓库内的 freestanding crt0 消费。它刻意省略 auxv、TLS 和 dynamic linker 状态。
Process-local file descriptor 与 VFS 壳层是内核管理的 lifecycle 状态，不由初始用户栈构造。

## ring3 进入

x86_64 运行期 user mode 支持由 `kernel/core/proc/user_mode.cc` / `user_mode.s` 提供：

- 新 GDT 保持 kernel code/data/stack selector `0x08/0x10/0x18` 不移动。
- 新增 user data selector `0x23`、user code selector `0x2b` 和 TSS selector `0x30`。
- `init_user_mode()` 加载 GDT 与 TSS，`set_tss_rsp0()` 在进入用户态前设置 kernel return stack。
- `enter_user_mode()` 构造 `SS:RSP/RFLAGS/CS:RIP` frame 并通过 `iretq` 进入 CPL3。

## syscall 与 fault

- `VECTOR_SYSCALL = 0x80` 是唯一放宽为 DPL=3 的 IDT gate；exception/IRQ gates 不放宽。
- `SYS_WRITE` 验证用户 buffer 范围、present/user bit 和最大长度，然后输出 `BIGOS_USER_WRITE_SYSCALL`；非法用户 buffer 会 fault 当前进程，并使用同一个 safe reaper 边界。
- `SYS_EXIT` 标记当前进程 terminated/reap-pending、记录 exit code、恢复 kernel root，并进入 scheduler 延后回收退出路径。
- `SYS_WAIT` 暴露最小 wait ABI，并与其它可阻塞 syscall 路径使用同一个 `sched::can_block()` guard。普通用户进程 syscall 在 scheduler context 和 IF 状态允许时可以阻塞；不支持的上下文返回确定性 wait 错误。
- 用户态 `#PF` 通过 saved `CS` 的 CPL 识别。受支持的 VMA-backed demand-zero 与 COW
  fault 由 bounded 用户 fault 路径处理；非法或不支持的 fault 输出
  `BIGOS_USER_PAGE_FAULT`，标记进程 faulted/reap-pending，并使用 safe reaper 边界。
- 内核态 `#PF` 保持既有 `BIGOS_PAGE_FAULT` 诊断-only 语义。
- idle-loop reaper 在 active stack/root 检查通过后释放用户地址空间和 kernel stack，然后输出 `BIGOS_USER_RECLAIMED`。
