## Why

BigOS 已具备进程生命周期、bounded ELF64 `exec`、fd/VFS 壳层和安全地址空间 teardown，但用户内存仍主要依赖页表探测与加载期固定映射，缺少描述用户虚拟地址策略的内核对象。阶段 14 需要在 demand paging、COW、`fork` 和用户态运行库之前，引入 VMA 与最小用户内存 API，使 `brk`、匿名映射、用户栈增长和用户 buffer 校验具备统一边界。

## What Changes

- 新增进程拥有的 VMA 集合，用于描述用户低半区代码、数据、heap、anonymous mapping、stack guard/stack-growth 区域及其权限。
- 新增最小 `brk` 用户内存 API，用 VMA 策略扩展或收缩进程 heap，并把分配、映射、失败回滚和进程终止语义固定在内核边界。
- 新增受限匿名映射 API，支持 page-aligned、bounded、non-file-backed 的用户映射，不引入文件映射、共享映射或 POSIX `mmap` 完整语义。
- 将用户地址范围验证从“仅页表 present 探测”提升为“先检查 VMA 权限和范围，再按需要检查 present 映射”的模型。
- 为用户栈定义 guard、最大增长范围和受控增长策略；在未实现 demand paging 前，缺页恢复只允许发生在明确的 stack-growth 受控路径，其他用户 `#PF` 仍按 fault 终止处理。
- 更新 exec、exit/reaper 和 syscall 边界，使 VMA 元数据随进程镜像创建、替换、终止和 teardown 一起维护。
- 非目标：不实现通用 demand paging、COW、`fork`、文件映射、共享内存、swap、page cache、writable filesystem、信号、SMP、完整 POSIX `mmap`/`munmap`/`mprotect` 语义或用户态 libc。

## Capabilities

### New Capabilities

- `vma-user-memory-api`: 定义进程 VMA 集合、`brk`、受限匿名映射、VMA-backed 用户范围验证、用户栈增长策略和对应验证要求。

### Modified Capabilities

- `user-address-space-vmem`: 用户映射和用户范围判断需要接受 VMA 策略约束，明确 VMA 与页表映射之间的职责边界。
- `address-space-lifecycle`: 地址空间 teardown 需要释放 VMA 元数据，并保证 VMA 与页表/物理页资源释放顺序安全。
- `process-lifecycle`: 进程对象、`exec` commit/rollback、exit/fault/reaper 生命周期需要拥有并维护 VMA 集合。
- `syscall-entry`: `int 0x80` syscall 分发需要覆盖最小用户内存 API，并基于 VMA 权限验证用户 buffer。

## Impact

- 影响子系统：`src/mm` 用户地址空间和页表映射路径、`src/kernel/proc` 进程生命周期与 exec、`src/kernel/syscall` syscall ABI/dispatch、`include/bigos` 公共内核接口，以及相关 source-level tests 与 smoke 配置。
- 架构假设：x86_64 单核 long mode，当前 `#PF` handler 仍以诊断/受控终止为默认行为；仅在明确的 stack-growth 场景允许用户缺页恢复。
- 内存布局假设：保留 higher-half kernel、direct map、`KVMEM_BASE`、recursive self-mapping 和用户低半区边界；本 change 不移动 linker 地址、boot handoff 地址或页表自映射地址。
- 模拟器和工具链假设：构建以 `xmake` 和 `x86_64-elf-gcc/g++` 为主；运行时验证优先 QEMU headless 串口 marker，必要时用 Bochs 或 QEMU+Bochs 交叉验证；Python 辅助检查通过 `uv run ...` 执行。
- 磁盘布局假设：继续使用当前 Legacy BIOS/MBR/exFAT raw image 和只读用户 ELF 来源；本 change 不要求修改 boot sector、磁盘分区格式、UEFI/OVMF、virtio/AHCI/NVMe 或 writable filesystem。
