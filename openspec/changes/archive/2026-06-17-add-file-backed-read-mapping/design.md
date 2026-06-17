## Context

BigOS 当前的用户态缺页恢复由统一用户缺页入口处理，仅识别匿名 backing（匿名映射、heap、向下增长栈、ELF 零填充）的惰性零页物化；对“非匿名 backing 且无恢复策略”的 CPL3 缺页一律按文档化 fault-to-lifecycle 路径确定性 kill。文件数据的读取目前依赖 `read`/`lseek` 同步搬运。

底层已具备本变更所需的两块基础：

- VMA 模型（`vma-user-memory-api`）为每个进程维护有界 VMA 集合，记录范围、权限、purpose、backing、growth、ownership 与物化记账，并支撑范围校验、`fork`/COW 复制与 teardown。
- page/buffer cache（`page-buffer-cache`）以（块设备, 块号）为键缓存固定大小块，读路径优先命中缓存、未命中经现有块读路径装入，且只在允许阻塞的进程上下文进行装入。

本设计在这两块基础之上引入有界 file-backed 只读映射，不触碰 boot/链接/中断向量/页表自映射地址，不改变扇区/块大小契约。

## Goals / Non-Goals

**Goals:**

- 让用户进程把一个可读文件的页对齐区间映射为只读、私有、按需分页的用户内存。
- 复用统一缺页入口与 page/buffer cache：首次访问 file-backed 页时经缓存读入对应文件块并建立只读 PTE。
- 让 VMA 模型识别 file-backed backing（支撑文件引用 + 文件内偏移），使范围校验、`fork`、exec 替换、teardown 一致处理。
- 提供默认关闭、可复现的运行时 smoke。

**Non-Goals:**

- 不支持 file-backed 可写/写回映射、`MAP_SHARED` 跨进程可写共享、`mprotect`、`MAP_FIXED` 覆盖、swap。
- 不实现完整 POSIX `mmap`/`munmap` 语义面。
- 不改变匿名 demand paging、COW、缓存落盘语义与扇区/块大小契约。
- 不引入新存储/设备后端。

## Decisions

### 决策 1：新增独立 backing 类型而非复用匿名分支

为 VMA 新增 `file-backed`（只读、私有）backing 类型，携带支撑文件引用与起始文件偏移；缺页入口据此选择只读物化分支。

- 备选：把 file-backed 当作“预填充内容的匿名页”。否决，因为这要求在映射时即读文件、丧失惰性，且无法在 `fork` 时共享底层缓存。
- 选择独立 backing 可让范围校验、`fork` 复制、teardown 显式区分匿名与文件页所有权。

### 决策 2：物化经 page/buffer cache 读入只读页

首次访问 file-backed 页时，统一缺页入口在允许阻塞的进程上下文计算 `文件偏移 = VMA 文件起始偏移 + (faulting_page - VMA 起始)`，经 VFS/page-buffer-cache 读取覆盖该页的文件块，把内容放入一个只读用户页帧，建立 **只读、非可执行（除非显式只读可执行策略）** 的用户 PTE，并推进 VMA 物化记账。

- 越过文件实际长度的页（文件尾与 VMA 末页之间的尾部）按只读零填充处理还是越界 kill？决策：超出文件长度且超过映射允许范围的访问按 **确定性 kill**；文件尾页内不足一页的尾部以 **零填充** 补齐（与 ELF 零填充语义一致），保持页粒度可预测。

### 决策 3：缺页分支与现有 present-bit kill 规则共存

file-backed 只读分支处理 **not-present** 读缺页。任何对 file-backed 只读页的写访问（present 或 not-present 的写）仍走既有确定性权限违例 kill，不进入 COW，也不进入只读物化。

### 决策 4：fork 共享底层只读缓存

`fork` 时 file-backed VMA 作为元数据复制（范围、文件引用、偏移、物化记账）。已物化的只读页在父子间共享，并依赖底层 page/buffer cache 的只读块；不深拷贝、不进入 COW。子进程对同一页的首次访问若尚未物化，则各自经缓存物化。

### 决策 5：映射请求 ABI 采用专用 syscall

新增一个专用 syscall（如 `SYS_MAP_FILE`），仅**追加**到 `include/bigos/syscall.h` 既有枚举末尾，不移动任何既有向量/编号，不碰 `int 0x80` 软中断向量。参数沿用既有寄存器 ABI：`rdi=fd, rsi=offset, rdx=len, r10=permissions, r8=flags`，返回映射用户地址或负 errno。校验：fd 指向可读常规文件、offset/length 页对齐且不溢出、目标区间落在受支持用户低半区且不与既有 VMA 重叠、权限不含写与 W+X。校验失败确定性返回负错误码，不发布部分 VMA。

- 备选：扩展现有 `SYS_MAP_ANON`，用 flag 位区分匿名/文件并复用未用寄存器传 fd/offset。否决，因为 file-backed 的 fd/offset/文件长度校验与匿名映射无交集，混入同一入口会让现有匿名路径承担额外分支与回归风险，违背"小而显式、最小隐藏控制流"的低层风格。
- 选择专用 syscall 与决策 1（独立 file-backed backing 类型）保持前后一致：backing 与调用面都独立，匿名路径零改动。新增编号成本极低（dispatcher 仅多一个 case）。

## Risks / Trade-offs

- [文件在映射存活期间被改名/删除/截断] → 受现有 constrained rename 与有界 FS 语义限制；映射持有支撑文件引用，物化按当时文件块读取。本变更不承诺与并发写的 POSIX 一致性，越界访问按 kill 处理，并在 spec 中明确为非目标。
- [缓存装入在不可阻塞上下文被触发] → 复用 page-buffer-cache 的进程上下文边界：IRQ/调度临界区/preemption-disable 上下文不发起阻塞块 IO，确定性失败或进入诊断路径。
- [文件页与缓存块大小不一致导致跨块读取] → 物化以页为单位，可能跨多个缓存块；按页聚合多块读取，任一块 IO 失败则该次物化确定性失败并 kill，不发布部分映射。
- [只读共享页被错误标记可写] → 物化始终建立只读 PTE，写访问统一走权限违例 kill；source-level 检查覆盖该不变量。
- [越界尾页语义混淆] → 明确：文件尾页内零填充，超出 VMA/文件范围越界 kill；smoke 覆盖该边界。

## Migration Plan

- 纯增量能力，默认 init/shell 路径不变。映射调用面与 smoke 默认关闭/可选。
- 回滚：移除映射 backing 分支与 syscall 面后，统一缺页入口回退到既有“非匿名 backing 即 kill”行为，匿名/COW/缓存语义不受影响。

## Open Questions

- 暂无未决项。映射调用面已在决策 5 落定为专用 syscall（`SYS_MAP_FILE`），最终编号在实现阶段对照 `include/bigos/syscall.h` 既有枚举末尾追加，不移动既有向量。
