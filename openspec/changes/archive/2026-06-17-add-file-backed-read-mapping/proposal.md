## Why

当前用户态内存映射仅支持匿名 backing（heap、受限匿名映射、向下增长栈、ELF 零填充），统一缺页入口对“非匿名 backing 且无恢复策略”的访问一律确定性 kill。用户程序无法把一个常规文件按页映射进地址空间，只能反复 `read`/`lseek` 搬运数据，限制了更复杂程序的实现。本变更在既有 demand paging 与 page/buffer cache 之上引入 **file-backed 只读映射**，让用户程序可以把只读文件按页映射访问，作为地址空间与映射能力成熟的第一步。

## What Changes

- 新增 **file-backed 只读用户映射** 能力：允许用户进程请求把一个可读文件的某个页对齐区间映射为只读、私有、按需分页的用户内存。
- 扩展统一用户缺页入口：为 file-backed VMA 增加一个只读物化分支——首次访问时通过现有 page/buffer cache 读入对应文件块，建立只读用户 PTE，并推进物化记账；越界（超过文件范围）按文档化用户 fault 路径处理。
- 扩展 VMA 模型：新增 file-backed backing 类型，记录支撑文件引用与文件内偏移，使范围校验、缺页恢复、`fork`、`exec` 替换与进程拆除都能识别该类区间。
- 扩展 `fork`：file-backed 只读映射在 `fork` 时作为 VMA 元数据复制并继续共享底层只读页缓存，而非深拷贝。
- 新增默认关闭的运行时 smoke，覆盖“映射后首次访问命中正确文件内容”“越界访问确定性 kill”“非法/不支持请求被拒绝”等行为。

### 非目标 / Non-Goals

- 不引入 file-backed **写回映射**（`MAP_SHARED` 可写、脏页回写）。
- 不引入 `MAP_SHARED` 跨进程可写共享、`mprotect` 改写权限、`MAP_FIXED` 覆盖、swap。
- 不实现完整 POSIX `mmap`/`munmap` 语义面；本变更仅提供有界只读 file-backed 子集。
- 不改变现有匿名 demand paging、COW、page/buffer cache 落盘语义或扇区/块大小契约。
- 不引入新的存储/设备后端，不改变 boot/链接/中断向量/页表自映射地址。

### 假设 / Assumptions

- 架构：x86_64-only，单核，Legacy BIOS/MBR/exFAT 默认基线。
- 内存布局：file-backed VMA 落在受支持的用户低半区，避开内核高半区、direct-map、KVMEM 与递归自映射范围。
- page/buffer cache 装入只在允许阻塞的进程上下文进行，IRQ/调度临界区/preemption-disable 上下文不发起阻塞块 IO。
- 工具链：x86_64-elf-gcc 交叉编译，QEMU headless serial-marker 用于 smoke 验证。

## Capabilities

### New Capabilities

- `file-backed-read-mapping`: 有界只读 file-backed 用户映射——映射请求建立 file-backed 只读 VMA、首次访问经 page/buffer cache 按需物化只读页、越界与非法请求确定性失败、`fork` 共享底层只读缓存，以及默认关闭的可复现验证。

### Modified Capabilities

- `demand-paging`: 统一用户缺页入口增加 file-backed 只读物化分支，作为现有“非匿名 backing 无恢复策略即 kill”的受控例外。
- `vma-user-memory-api`: VMA 模型新增 file-backed backing 类型与文件偏移记账，范围校验、`fork` 复制与物化记账识别该类区间。

## Impact

- 受影响子系统：`kernel/core/proc`（VMA 模型、统一缺页入口、`fork`/exec/teardown）、`kernel/core/fs`（page/buffer cache 与文件块读取接口）、`kernel/core/syscall`（映射请求 ABI）。
- 受影响头文件：`include/bigos/proc.h`、`include/bigos/syscall.h`、`include/bigos/fs/vfs.h`。
- 用户态：新增可选的只读映射调用面与对应 smoke 程序；默认 init/shell 行为不变。
- 不改变 boot 固定地址、higher-half base、direct-map 窗口、`KVMEM_BASE`、递归自映射、syscall 向量与 exception/IRQ EOI 语义。
