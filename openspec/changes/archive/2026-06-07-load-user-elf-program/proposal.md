## Why

阶段 7 已经把只读 block device 与 exFAT 文件读取能力下沉到内核运行时，但阶段 6 的首个用户程序仍来自 flat embedded image，无法验证“磁盘文件 -> ELF program headers -> 用户地址空间 -> ring3”的真实加载链路。现在需要把这条链路作为阶段 8 的独立 OpenSpec change 固化下来，为后续 exec、多进程和更完整的用户程序产物管理提供基础。

## What Changes

- 新增内核态用户 ELF64 loader：从只读 exFAT 路径读取一个 bounded ELF64 用户程序文件，校验 ELF header 与 program headers。
- 按 ELF `PT_LOAD` 段权限映射用户 text、rodata/data、bss，并为用户栈建立显式映射。
- 增加默认关闭的 ELF 用户程序 smoke，验证文件系统读取、ELF segment loading、ring3 entry、`SYS_WRITE`/`SYS_EXIT` 闭环。
- 将最小 `Process` 与 scheduler thread / safe reaper 边界进一步绑定，明确 ELF 加载失败、用户退出和 fault 后的资源归属。
- 保留阶段 6 flat embedded user program smoke 作为独立回归路径；本 change 不移除现有 embedded smoke。

## Capabilities

### New Capabilities

- `user-elf-program-loader`: 定义 BigOS 从内核只读文件系统加载 ELF64 用户程序、按 segment 权限映射用户地址空间、进入 ring3 并通过默认关闭 smoke 验证的能力。

### Modified Capabilities

- `first-user-program`: 明确阶段 6 的 flat embedded 首个用户程序 smoke 保持可用，同时允许新的 ELF loader runtime 复用最小 `Process`、ring3 entry、syscall 和 safe teardown 边界，而不把文件系统依赖并入原 embedded smoke。

## Impact

- 受影响子系统：`src/kernel/proc`、`src/kernel/syscall`、`src/mm` 用户地址空间 map/teardown、`src/kernel/fs`/block read 调用路径、xmake smoke 配置、`tools/boot_debug.py` 相关 image 验证资产。
- 新增或扩展 API：用户 ELF loader 内部 API、ELF smoke 入口、用户程序产物安装/查找约定，以及 loader 到 `Process` 创建/回收路径的错误返回。
- 依赖能力：阶段 5 syscall entry、阶段 6 first user program runtime、阶段 6.5 address-space lifecycle、阶段 7 block/fs read。
- 架构假设：单核 x86_64、4KiB page、当前 higher-half kernel/direct map/KVMEM/self-mapping 布局不变，`int 0x80` syscall gate 可由 CPL3 进入，exception/IRQ gate 仍保持 ring0-only。
- 磁盘与工具链假设：Bochs raw image 上存在受控 exFAT 分区，ELF 用户程序以静态 freestanding 产物放入约定路径；构建使用 xmake 与 `x86_64-elf-gcc/g++`，Python 辅助命令通过 `uv run` 执行。
- 非目标：不实现 fork/exec 语义全集、动态链接、用户态 libc、文件描述符 syscall、mmap/brk、demand paging、COW、信号、抢占调度、多核 TLB shootdown、写文件系统或通用 VFS。
