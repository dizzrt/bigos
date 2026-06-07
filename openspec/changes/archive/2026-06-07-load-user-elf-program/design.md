## Context

BigOS 当前已经具备运行第一个 ring3 用户程序的最小闭环：`Process`、派生用户页表 root、TSS/RSP0、`iretq` 进入、`SYS_WRITE`/`SYS_EXIT`、用户 fault 标记和 safe reaper 边界均已建立。阶段 7 又新增了内核态只读 block device 与 exFAT mount/path lookup/bounded read 能力，因此阶段 8 可以把用户程序来源从 flat embedded image 推进到磁盘上的 ELF64 文件。

本设计覆盖 `src/kernel/proc`、用户地址空间 map/teardown、`src/kernel/syscall` 的现有用户闭环、只读 FS 调用路径、xmake smoke 配置与测试镜像资产。设计不移动 boot fixed addresses、higher-half base、direct map、`KVMEM_BASE`、self-mapping 地址、syscall vector 或既有 BootInfo ABI。

## Goals / Non-Goals

**Goals:**

- 从约定 exFAT 路径读取一个 bounded ELF64 用户程序文件，例如 `/boot/user/init.elf`。
- 校验 ELF64 header 与 `PT_LOAD` program headers，仅接受静态、低半区、用户态可映射的 `ET_EXEC` 产物。
- 按 segment 权限映射用户 text、rodata/data 和 bss，并创建用户栈页。
- 复用阶段 6 的 ring3 entry、syscall ABI、用户 fault 终止和阶段 6.5 的 safe teardown 边界。
- 新增默认关闭的 ELF 用户程序 smoke，输出可脚本判定的 `BIGOS_USER_ELF_` / `BIGOS_USER_EXIT` marker。

**Non-Goals:**

- 不实现 fork/exec/wait、动态链接、PIE/ASLR、用户态 libc、文件描述符 syscall 或 shell。
- 不实现 demand paging、COW、mmap/brk、用户栈自动增长或信号。
- 不把 block/FS read 变成 IRQ-handler-safe、异步、可阻塞队列或通用 VFS。
- 不引入抢占调度、优先级调度、SMP、跨核 TLB shootdown 或多进程公平性。
- 不移除 flat embedded `user_program_smoke`，它继续作为不依赖 FS 的回归路径。

## Decisions

### Decision: 使用独立 `user_elf_smoke` 开关

ELF 文件加载路径使用新的默认关闭开关，例如 `xmake f --user_elf_smoke=y`。该开关可以复用或显式包含 `src/kernel/proc/**`，但不改变普通 boot，也不让阶段 6 的 flat embedded smoke 依赖 block/FS。

替代方案是直接替换 `user_program_smoke`。该方案会把原本无需磁盘 FS 的 ring3 回归路径变成 FS 相关路径，降低故障定位能力，因此不采用。

### Decision: loader 读取完整 bounded ELF 文件到内核缓冲区

第一版 loader 通过只读 FS API 将 ELF 文件读取到 bounded kernel buffer，再解析 ELF header 和 program headers。文件大小上限固定为早期 smoke 可接受的常量，分配发生在普通非 IRQ kernel context。

替代方案是按需读取 ELF header 和 segment 数据。该方案更接近真实 loader，但需要更多随机读取状态、错误回滚点和缓存策略；阶段 8 优先验证端到端路径，因此先采用完整 bounded 读取。

### Decision: 仅接受静态 ELF64 `ET_EXEC` 和 `PT_LOAD`

loader 只接受 x86_64 little-endian ELF64、`ET_EXEC`、目标 machine 为 x86_64、entry 位于用户低半区且落在已映射 executable segment 内。仅处理 `PT_LOAD`，拒绝 interpreter、dynamic、TLS、超大对齐、重叠段、越界虚拟地址和可能碰撞内核高半区的地址。

替代方案是支持 PIE、动态链接或更完整的 ELF program header 类型。它们需要 relocation、动态装载器和用户运行时，本阶段不引入。

### Decision: segment 映射以 page-rounded ELF 范围为单位

每个 `PT_LOAD` 按 `p_vaddr` 向下页对齐，`p_memsz` 向上覆盖到页边界；`p_filesz` 字节来自文件，`p_memsz - p_filesz` 清零为 bss。权限由 ELF flag 转换为页属性：`PF_X` 清 NX，`PF_W` 置 writable，所有用户 segment 置 user bit；非 executable 数据页必须 NX。

如果同一页同时覆盖 text 和 writable data，第一版 loader 应拒绝或按保守策略失败，而不是创建 W+X 用户页。

### Decision: 失败路径必须回滚到 safe process 状态

ELF header 校验、文件读取、segment 分配、页表映射或 copy 失败时，loader 必须停止进入 ring3，并释放已经拥有的用户 leaf pages、动态页表页和临时 kernel buffer。若 `Process` 已创建但未运行，回收可直接在当前 safe kernel context 执行；若已经进入用户态，仍使用既有 terminated/faulted -> reaper 边界。

### Decision: 用户程序产物由构建/镜像工具安装到约定路径

xmake 或现有镜像安装工具负责把 freestanding 用户 ELF 产物放入 raw image 的 exFAT 分区。loader 只消费内核 FS API 暴露的路径，不直接理解宿主文件系统或 bootloader-only helper。

## Risks / Trade-offs

- ELF 文件一次性读入可能占用较多早期内存 -> 使用固定文件大小上限、普通非 IRQ context 分配，并在失败路径释放临时 buffer。
- Segment 权限转换错误可能产生 W+X 或 kernel-accessible 用户映射 -> 源码级检查覆盖 `PF_X/PF_W` 到 page attr 的转换，并拒绝 W+X/高半区/溢出范围。
- FS/ATA PIO smoke 受 Bochs serial、ROM、image lock 或显示环境影响 -> validation 中区分构建/源码检查、离线 image 校验和 runtime marker 风险。
- ELF loader 与 flat embedded smoke 共用 `Process` 可能污染原回归路径 -> 使用独立开关与入口函数，保留 embedded smoke 的无 FS 依赖契约。
- 当前 scheduler 仍 cooperative 且无多进程公平性 -> ELF smoke 只运行一个用户进程，退出后走 safe reaper，不承诺多进程调度。

## Migration Plan

- 新增 `user_elf_smoke` 默认关闭开关和用户 ELF 产物安装规则，不改变默认 `xmake` boot 行为。
- 先实现 loader 的纯解析/校验与源码级测试，再接入 `Process` 创建、地址空间映射和 ring3 entry。
- 保留 `user_program_smoke` 的 flat embedded image 路径，作为 rollback 和定位工具。
- 若 runtime smoke 不稳定，仍提交构建、OpenSpec 校验、源检查和离线 raw image 校验结果，并记录 Bochs blocker。

## Resolved Decisions

- ELF 用户程序路径固定为 `/boot/user/init.elf`。第一版不做 xmake 可配置路径，避免 smoke、镜像安装和 loader 查找策略分叉；后续 exec/多程序阶段再引入路径参数化。
- 第一版用户栈继续沿用阶段 6 的单页栈。阶段 8 的目标是验证 filesystem-backed ELF loader 到 ring3 的端到端链路，单页栈足够承载最小 `SYS_WRITE`/`SYS_EXIT` smoke，并能减少栈增长、guard page、多页回收等额外变量。
- 新增 `BIGOS_USER_ELF_LOAD_PASSED` 作为 loader 成功 marker，并继续以用户程序 `SYS_WRITE` payload 和 `BIGOS_USER_EXIT` 作为端到端 oracle。这样可以区分 ELF 文件读取/校验/映射成功与后续 ring3 entry、syscall 或退出路径失败。
