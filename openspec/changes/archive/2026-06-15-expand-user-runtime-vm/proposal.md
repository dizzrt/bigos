## Why

BigOS 已完成有界 libc、文件系统、进程、终端与 shell 的基础成熟化，下一步需要把用户程序运行时依赖的 VM 契约从“能跑小程序”推进到“能支撑更复杂程序”。现在整理用户地址空间布局、memory mapping 策略与 loader 边界，可以为后续动态链接或更大用户程序生态铺路，同时避免提前承诺完整 POSIX VM 语义。

## What Changes

- 明确用户进程地址空间布局契约，包括 ELF 段、heap、匿名映射、stack、guard/growth 区域、未来 runtime 预留区与内核高半区隔离边界。
- 收紧 VMA-backed memory mapping 策略，使 `brk`、受限匿名映射、lazy materialization、COW、用户范围校验与 teardown 共享一致的权限、ownership 和失败回滚规则。
- 扩展 ELF/user image loader 边界，要求 loader 产出完整的用户运行时 image 描述，并在提交前完成 VMA、页表、初始栈、`argv`/`envp` 和资源 ownership 校验。
- 增加未来动态链接准备要求，预留必要的 ABI/地址空间/loader 扩展点，但不实现 shared libraries、动态加载器或完整动态链接语义。
- 保持现有 x86_64 Legacy BIOS 运行目标、单核执行模型、`int 0x80` syscall ABI、页表高半区布局、direct map、recursive self-mapping 和当前磁盘布局不变。

## Capabilities

### New Capabilities

- `user-runtime-vm-layout`: 定义更成熟的用户程序运行时 VM 布局、runtime 预留区、image commit 边界和未来动态链接准备契约。

### Modified Capabilities

- `user-address-space-vmem`: 扩展用户地址空间页表根、用户低半区布局、VMA 授权映射和 teardown ownership 的要求。
- `vma-user-memory-api`: 扩展 VMA-backed `brk`、匿名映射、stack growth、用户范围校验、fork/COW 和失败回滚规则，使其符合统一运行时 VM 布局。
- `demand-paging`: 扩展统一用户缺页处理对 runtime layout、匿名 lazy materialization、COW 和确定性失败语义的覆盖。
- `user-elf-program-loader`: 扩展 ELF loader 与 exec/image commit 边界，使 loader 明确产出 runtime image、初始栈和动态链接准备信息。

## Impact

- 影响子系统：`kernel/core/proc`、`kernel/mm`、`kernel/core/syscall`、`kernel/core/fs` 的用户地址空间、VMA、缺页、exec/loader、用户指针校验和进程 teardown 路径。
- 影响用户态：`user` 中静态 ELF 程序、crt0/libc 启动约定、`argv`/`envp` 初始栈布局和未来 runtime ABI 预留。
- 影响验证：需要覆盖 source-level VM/layout 不变量、OpenSpec strict validation、窄范围 `xmake` 构建，以及可用时的 QEMU/Bochs 默认关闭 smoke；若本地 cross-toolchain 或 emulator 不可用，必须记录跳过项和剩余 bootability 风险。
- 非目标：不实现广泛 file-backed `mmap`、shared mapping、swap、page cache、shared libraries、dynamic loader、完整 POSIX `mmap`/`exec`/job control、SMP、UEFI runtime parity、持久完整可写文件系统或新的存储/ISA backend。
- 假设：本 change 继续面向 x86_64 Legacy BIOS/MBR/exFAT 默认运行路径，保持当前 higher-half kernel、direct map、KVMEM、recursive self-mapping、syscall vector、interrupt/EOI 语义和 raw image 打包边界不变。
