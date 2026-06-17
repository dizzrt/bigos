## Context

BigOS 当前已经具备有界用户态：静态 ELF 程序、PID-1 init、`/bin/sh`、VMA-backed 用户内存 API、匿名 demand paging、`fork`/COW、`execve`、fd/VFS、pipe、signals 和 bounded libc-style 支持。kernel thread scheduler capability3 的问题不是“让第一个用户程序跑起来”，而是把更复杂用户程序会依赖的 VM/runtime 契约整理成稳定边界。

当前相关能力分散在用户页表派生、ELF loader、VMA 集合、缺页恢复、用户范围校验、进程 lifecycle 和 user crt0/libc 约定中。若继续按局部功能扩展，容易出现 loader 直接发布页表、VMA 与页表权限不一致、`brk`/匿名映射/stack growth 使用不同失败语义、未来动态链接没有地址空间预留等问题。

本设计继续假设 x86_64 Legacy BIOS/MBR/exFAT 是默认运行目标，保持 single-core、mostly synchronous、有界用户态模型。设计不改变 higher-half kernel、direct map、KVMEM、recursive self-mapping、syscall vector、interrupt/EOI、raw image 布局或当前 cross-toolchain/emulator 前提。

## Goals / Non-Goals

**Goals:**

- 定义统一的用户运行时 VM layout，使 ELF 段、heap、匿名映射、stack、guard/growth、runtime 预留区和内核高半区隔离边界有稳定规则。
- 将 ELF loader、exec/image commit、VMA 发布、页表映射、用户栈初始化和资源 ownership 串成可回滚的提交流程。
- 让 `brk`、受限匿名映射、demand paging、COW、用户范围校验和 teardown 基于同一组 VMA 权限、backing、purpose 和 materialization accounting。
- 为未来 dynamic linking 保留地址空间与 ABI 扩展点，但只记录准备契约，不引入动态加载器实现。
- 增加可复现验证要求，覆盖 layout 不变量、权限不变量、失败回滚、不改变架构关键地址/向量/EOI 语义。

**Non-Goals:**

- 不实现广泛 file-backed `mmap`、shared mapping、swap、page cache 或完整 POSIX `mmap`。
- 不实现 shared libraries、dynamic loader、`PT_INTERP`、`ET_DYN` 运行、symbol relocation 或完整动态链接。
- 不改变当前 syscall ABI、job control、terminal process groups、SMP、UEFI runtime parity、持久完整可写文件系统或新存储/ISA backend。
- 不把用户态提升为完整通用 OS runtime；仍保持 bounded POSIX-like subset。

## Decisions

### Decision: 以 runtime layout capability 作为 kernel thread scheduler capability3 的主契约

将新增 `user-runtime-vm-layout`，用于描述用户程序 image 在低半区的结构、预留区、提交边界和未来动态链接准备。已有 specs 继续分别约束页表、VMA、缺页和 loader，但它们都引用同一 runtime layout 规则。

备选方案是只修改现有 `user-address-space-vmem` 或 `user-elf-program-loader`。该方案会让 layout、loader、VMA 与 demand paging 的共同约束散落在多个 capability 中，后续 archive 后也难以看出 kernel thread scheduler capability3 的核心目标。

### Decision: loader 只准备 image，commit 边界统一发布

ELF loader 应产出一个 bounded runtime image 描述，包括 loadable segment VMAs、segment permissions、entry point、初始 stack/guard、heap seed、`argv`/`envp` 布局和资源 ownership。真正对进程可见的 VMA 集合、页表 root、user stack 和 process state 应在 image commit 阶段一次性发布。

这样可以避免失败路径留下半提交页表或半发布 VMA。备选方案是 loader 边加载边发布到当前进程；实现更直接，但 rollback、exec failure 和 parent/child lifecycle 语义更难保持确定性。

### Decision: VMA 是用户内存策略来源，页表是 materialized state

所有用户低半区映射必须先有兼容 VMA，再允许发布 PTE。VMA 描述 purpose、backing、permissions、growth、ownership 和 materialization accounting；页表只表示当前已物化的翻译状态。用户范围校验必须同时考虑 VMA 权限和页表可访问性，不能只靠 present PTE 授权。

备选方案是继续让不同路径直接检查页表或局部元数据。该方案短期改动小，但会放大 heap、anonymous mapping、stack、ELF bss、COW 在权限和 teardown 上的不一致风险。

### Decision: dynamic linking 只做准备，不做运行时实现

本 change 只要求保留 runtime 预留区、loader metadata 扩展点、ABI 边界说明和 unsupported feature 的确定性拒绝。`PT_INTERP`、shared object、relocation、runtime linker entry、lazy binding 等仍必须被拒绝或保持未实现状态。

备选方案是在 kernel thread scheduler capability3 直接实现 dynamic loader。该方案用户可见价值大，但会同时依赖更强 VM、filesystem、loader、ABI、libc 和 toolchain 契约，容易把 kernel thread scheduler capability3 从“铺地基”变成不可控的大里程碑。

## Risks / Trade-offs

- [Risk] Layout 固定过早，后续 dynamic linking 需要调整地址空间区域 → Mitigation: 只固定有界区间类别、排序、隔离和冲突规则，保留具体大小/基址作为实现常量并要求源码级验证。
- [Risk] commit/rollback 边界增加实现复杂度 → Mitigation: 将 image preparation 与 process publication 分层，要求失败路径统一释放未发布资源，已发布资源走 lifecycle/reaper。
- [Risk] VMA 与页表 accounting 不一致导致泄漏或错误授权 → Mitigation: 所有 mapping、unmapping、fault recovery、fork/COW、teardown 都通过 VMA ownership 与 materialization accounting 校验。
- [Risk] source-level 验证无法替代 emulator boot 行为 → Mitigation: 记录窄范围 `xmake` 构建和可用 emulator smoke；若 QEMU/Bochs/cross-toolchain 不可用，明确记录跳过项和剩余 bootability 风险。
- [Risk] 为未来 dynamic linking 做准备可能被误解为已支持 shared libraries → Mitigation: specs 中明确 `PT_INTERP`、`ET_DYN`、shared objects 和 dynamic loader 都是非目标，并要求 unsupported feature 确定性拒绝。
