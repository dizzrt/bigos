## Context

BigOS 用户态 libc 已有 bounded 子集：`user/libc/stdio.c` 把 `stdin`/`stdout`/`stderr` 实现为指向静态 `int` 的不透明指针，`stream_fd()` 仅识别这三个标准流并直接转发到 fd 0/1/2 的 `read`/`write`。`printf`/`fprintf`/`snprintf` 共享一个 `format_sink` formatter；`fopen`/`fclose`/缓冲/`fseek`/`feof` 等全部不存在。`string.h`/`stdlib.h`/`ctype.h` 只覆盖最常用的一小撮 helper。用户程序若要读写命名文件，只能直接用 `open`/`read`/`write`/`lseek`（裸 fd 路径），与多数“可移植标准小程序”默认依赖的缓冲 `FILE` 流不兼容。

内核侧已提供本变更所需的全部后端原语：`open`/`read`/`write`/`close`/`lseek`/`fsync` syscall 与 `brk`-backed 用户分配（`malloc`/`free`）。因此“更完整的 libc 子集”可以完全在用户态实现，不需要新增或修改任何 syscall、ABI、地址布局或内核代码。

约束：

- `user/**` 只能使用本仓库实现的 freestanding libc，不得假设 hosted runtime、异常、RTTI、线程或动态初始化。
- 公开头保持最小化，每个公开声明必须有实现或明确的非支持边界。
- 不改变启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或既有静态 `ET_EXEC` 装载 ABI 与 syscall number。
- 默认启动（libc smoke 关闭）必须与现状完全一致地进入 shell 并运行静态 `/bin/*`。

## Goals / Non-Goals

**Goals:**

- 在用户态把 `FILE` 升级为有界缓冲流对象：按文件名 `fopen`/`freopen`/`fclose`、缓冲 `fread`/`fwrite`、面向字符/行的 `fgetc`/`getc`/`fgets`/`fputc`/`putc`/`fputs`、单字节 `ungetc`、缓冲控制 `setvbuf`/`setbuf`（三模式）、刷新 `fflush`、流状态 `feof`/`ferror`/`clearerr`、`fileno`，以及基于既有 `lseek` 的有界字节定位 `fseek`/`ftell`/`rewind`。
- 把既有 `putchar`/`puts`/`printf`/`fprintf` 重接到缓冲流之上，复用现有共享 formatter，行为对既有调用者保持可观察一致（标准流默认行为不回退）。
- 补齐 portable 小程序高频 `string.h`/`stdlib.h`/`ctype.h` helper 与标准 freestanding 头子集（`stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h`）。
- 用代表性可移植小程序与默认关闭 smoke 验证缓冲流回环、定位、刷新、流状态与新增 helper 行为及确定性失败路径。

**Non-Goals:**

- 超出本子集的 hosted stdio：宽流、`tmpfile`/`tmpnam`、`fmemopen`/`open_memstream`、文本/二进制流换行转换语义差异、`freopen` 的 `path==NULL` 仅改模式变体。
- locale、多字节、宽字符、浮点格式化与解析、`scanf` 家族（除非后续变更显式新增消费者）。
- 完整 `fpos_t`/`fgetpos`/`fsetpos`、线程安全流锁（`flockfile`/`funlockfile`）、信号可中断 I/O、async I/O。
- file-backed `mmap`、动态链接/共享库化 `libc.so`、SMP、新 ISA/backend。
- 不改变任何内核 syscall、ABI、地址布局、磁盘布局或默认启动行为。

## Decisions

### 决策 1：`FILE` 为用户态结构体，缓冲在用户态实现，内核零改动

`FILE` 设计为一个 libc 内部结构体，至少持有：底层 fd、单个固定容量字节缓冲、缓冲内有效数据长度与当前读/写游标、缓冲模式（全缓冲/行缓冲/无缓冲）、`eof`/`error` 标志位、`ungetc` 单字节回推槽，以及一个“最后操作是读还是写”的方向标记。所有缓冲、行解析、定位偏移计算都在用户态完成，底层只调用既有 `read`/`write`/`lseek`/`close`/`fsync` syscall。

- 备选：在内核增加流式 I/O syscall。否决：会扩大 syscall ABI、违反“缓冲属于 libc 用户态职责”的边界，且与本变更“零内核改动”的目标冲突。

### 决策 2：标准流从“指向 int 的指针”迁移为“指向 `FILE` 的指针”，保持源级与行为兼容

把 `stdin`/`stdout`/`stderr` 改为三个静态 `FILE` 对象的地址，分别绑定 fd 0/1/2。`stdout` 默认行缓冲（tty 友好）、`stderr` 无缓冲（错误即时可见）、`stdin` 行/全缓冲读取。既有把 `stdout`/`stderr` 当作不透明指针传给 `fprintf`/`putchar`/`puts` 的代码无需改动即可继续工作；formatter 的 `format_sink` 增加“写入 FILE 缓冲”分支或经统一的 `fputc`/`fwrite` 出口。

数据流（写）：`printf` → formatter sink → `fputc`/缓冲累积 → 满/遇换行（行缓冲）/`fflush`/`fclose` → 一次 `write(fd)` 落盘。
数据流（读）：`fgets`/`fread` → 缓冲为空时一次 `read(fd)` 填充 → 从缓冲拷贝并推进游标，`ungetc` 优先消费回推槽。

- 备选：保留 int 别名、仅为命名文件新增独立 `FILE` 类型。否决：会产生两套不一致的 `FILE` 语义，且无法让 `fprintf(stdout, ...)` 与 `fwrite(...,stdout)` 共享缓冲。

### 决策 3：读写方向切换与定位时强制刷新，保证缓冲一致性

同一 `FILE` 在读写之间切换、或调用 `fseek`/`fflush`/`fclose` 时，必须先把写缓冲 `write` 落盘、并丢弃/重新对齐读缓冲与 `ungetc` 槽，再调整底层 `lseek` 偏移。`ftell` 返回“底层 `lseek` 当前偏移 ± 缓冲内未消费/未刷新字节”的修正值，使位置语义在有界范围内正确。这是缓冲流最易出错处，作为显式不变量在 spec 中固定。

### 决策 4：`fopen` 模式为有界子集，`freopen` 复用同一 `FILE` 对象

`fopen` 支持 `"r"`/`"rb"`、`"w"`/`"wb"`、`"a"`/`"ab"`、`"r+"`/`"w+"`/`"a+"` 的有界映射到现有 `O_RDONLY`/`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`/`O_APPEND`（以内核 `fcntl.h` 既有常量为准）。`b`（二进制）与无 `b`（文本）在 BigOS 行为相同（不做换行转换），并在头注释与 spec 中明确声明这一点。未识别模式串返回 NULL 并设 `errno=EINVAL`。

`freopen(path, mode, stream)` 纳入本期：先对传入 `stream` 执行 `fflush`+关闭底层 fd（忽略关闭错误以保证重定向语义），再以 `mode` 走与 `fopen` 相同的 `open` 流程，并把新 fd 与重置后的缓冲/标志写回**同一个** `FILE` 对象后返回该对象（使 `freopen("...","w",stdout)` 后 `stdout` 全局指针不变、底层已切换）。`path` 为 NULL 的“仅改模式”变体不在本期支持（返回 NULL 并设 `errno=EINVAL`）。打开失败时把 `stream` 置为已关闭/错误态并返回 NULL，不复用旧 fd。

- 备选：`freopen` 暂不实现、留非支持边界。否决：用户明确要求纳入本期，且重定向标准流是可移植小程序常见用法。

### 决策 4b：`setvbuf`/`setbuf` 暴露完整三模式（含调用方缓冲区与自定义容量）

`setvbuf(stream, buf, mode, size)` 支持 `_IOFBF`/`_IOLBF`/`_IONBF` 三种模式：

- 必须在“打开流之后、对该流首次 I/O 之前”调用，否则返回非零失败（与标准约束一致），避免在已有缓冲数据时切换导致状态不一致。
- `buf != NULL` 时使用调用方提供的缓冲区与 `size` 容量，`FILE` 记录“缓冲区非自有”，`fclose`/`freopen` 不释放它；`buf == NULL` 时由 libc 用 `malloc` 分配 `size` 容量（`size==0` 或 `_IONBF` 走无缓冲，单字节直通）。
- `setbuf(stream, buf)` 实现为 `setvbuf` 薄封装：`buf!=NULL` 等价 `_IOFBF` + `BUFSIZ`，`buf==NULL` 等价 `_IONBF`。

`FILE` 因此需区分“缓冲区是否 libc 自有”，以决定 `fclose`/`freopen`/再次 `setvbuf` 时是否释放旧缓冲。

- 备选：仅暴露“切换模式、固定容量、忽略调用方缓冲区”的有界版本。否决：用户明确要求不要有界版本，提供完整三模式语义。

### 决策 5：资源边界用确定性默认容量，分配失败走标准失败路径

`FILE` 对象与其默认缓冲经 `malloc` 分配（默认缓冲容量取固定常量 `BUFSIZ`，如 4096 字节；`setvbuf` 可改为调用方缓冲区或自定义容量，见决策 4b）；`fopen` 在 `malloc` 失败或底层 `open` 失败时返回 NULL 并保留/设置 `errno`，不泄漏已打开 fd 或已分配缓冲（失败回滚）。可对同时打开的 `FILE` 数量不设独立 libc 上限（受内核 fd 表与 `brk` 堆自然约束），但标准流不参与动态分配。`fclose` 先 `fflush` 再 `close`，最后释放 libc 自有缓冲与对象（调用方提供的缓冲区不释放）；对已关闭或 NULL 流的二次操作走确定性失败而非 UB 扩散。

### 决策 6：新增 helper 以“有实际消费者/验证”为准，比较器与可重入显式化

`qsort`/`bsearch` 使用调用方提供的比较器、无 locale；`strtok_r` 提供显式 `saveptr` 的可重入版本，不引入隐藏全局态的 `strtok`（除非后续显式新增并规格化）。`strtoll`/`strtoull` 复用既有 `strtol`/`strtoul` 的解析规则与 errno/溢出契约，仅扩展到 `long long` 宽度（在 ABI 类型支持下）。`memcmp`/`strcat`/`strncat`/`strspn`/`strcspn`/`strpbrk` 与扩充的 ctype 分类 helper 按标准 C 语义实现。未纳入项保持显式非支持边界。

### 决策 7：标准 freestanding 头直接复用工具链版本，`sys/types.h` 转引 `<stddef.h>`

已实测确认（`x86_64-elf-gcc 12.2.0`）：当前用户构建用 `-ffreestanding -nostdlib`、**不带 `-nostdinc`**，GCC 默认把自身 `include/` 与 `include-fixed/` 放入搜索路径，因此 `stddef.h`/`stdint.h`/`stdbool.h`/`stdarg.h`（在 `include/`）与 `limits.h`（在 `include-fixed/`）均可直接 `#include` 解析。故本仓库 `user/libc/include` **不**为这些标准头提供副本。

唯一真实冲突点是 `size_t`/`NULL` 在工具链 `<stddef.h>` 与本仓库 `user/libc/include/sys/types.h` 双重定义。处理方式：把 `sys/types.h` 中 `size_t`/`NULL` 改为 `#include <stddef.h>` 转引工具链定义（仅保留 BigOS 自有的 `ssize_t`/`off_t`/`mode_t`/`pid_t`），从根上消除双重 typedef/宏冲突。

- 备选：本仓库提供 `limits.h` 等协调副本。否决：工具链已可解析，自带副本会与工具链版本漂移、增加维护负担。
- 备选：保留 `sys/types.h` 自有 `size_t` typedef。否决：与 `<stddef.h>` 叠加 include 时按 GCC 的 `__SIZE_TYPE__`/include guard 虽多数可共存，但 `NULL` 宏与潜在 typedef 重定义仍有风险；转引是最干净的根治方案。

## Risks / Trade-offs

- [标准流语义回退风险]：把 `stdout` 改为行缓冲后，既有依赖“每次 `printf` 立即输出”的 smoke 或交互行为可能改变可见时序 → 缓解：`stderr` 保持无缓冲、在进程退出/`exit` 路径与 `fclose`/`fflush` 时刷新所有流，并在 smoke 中显式校验标准流输出顺序与完整性；必要时对 tty 场景采用行缓冲并在换行处刷新。
- [读写方向/定位缓冲不一致]：缓冲游标与底层 `lseek` 偏移不同步会导致数据错乱或位置错误 → 缓解：决策 3 的强制刷新/重对齐不变量作为 spec 显式要求，并由 `fseek`/`ftell`/`fread`/`fwrite` 交错的 smoke 覆盖。
- [`exit` 不刷新导致数据丢失]：用户程序调用 `exit` 或从 `main` 返回时若未刷新全缓冲流，写缓冲数据会丢失 → 缓解：在 libc 退出路径（`exit`/crt0 返回收敛点）注册/调用一次全流刷新，并在 smoke 中校验 `fwrite` 后不显式 `fclose` 直接退出仍能落盘。
- [内存/fd 资源泄漏]：`fopen` 部分失败或异常路径未回滚 → 缓解：决策 5 的失败回滚（释放缓冲、关闭已开 fd），并在 smoke 覆盖 `malloc` 失败模拟（如超大请求）与 `open` 失败路径。
- [标准头重复定义冲突]：工具链 `<stddef.h>` 与既有 `sys/types.h` 的 `size_t`/`NULL` 冲突 → 缓解：决策 7 把 `sys/types.h` 改为转引 `<stddef.h>`，并加构建期“只 include 标准 freestanding 头 + `sys/types.h`”的最小编译校验固化回归。
- [freopen 复用对象的状态一致性]：`freopen` 在同一 `FILE` 上换 fd，若旧缓冲/标志/方向未彻底重置会串味 → 缓解：决策 4 明确“先 flush+close 旧 fd 再重置整个对象”，并由“`freopen(stdout)` 后写入落到新文件且 `stdout` 指针不变”的 smoke 覆盖。
- [setvbuf 缓冲区所有权]：调用方提供缓冲区时 libc 误释放会造成 double-free/越界 → 缓解：决策 4b 在 `FILE` 标记“缓冲区是否 libc 自有”，`fclose`/`freopen`/再次 `setvbuf` 仅释放自有缓冲，并由“调用方栈缓冲 + `_IOFBF`”的 smoke 覆盖。
- [边界蔓延]：缓冲流容易诱导继续加 `scanf`/宽流/locale → 缓解：proposal/spec 显式非目标，未纳入接口不在公开头声明。

## Migration Plan

- 纯增量、默认关闭验证：新增 helper 与 `FILE` 流接口对既有裸 fd 路径无破坏；既有 `/bin/*` 可继续用裸 fd，迁移到 `FILE` 流为可选项。
- 标准流类型变更（int 指针 → `FILE` 指针）是源级兼容的最大风险点：先实现 `FILE` 模型与标准流绑定，跑既有 userland/默认启动回归确认输出无回退，再逐步接入 formatter 与新增接口。
- 回滚策略：本变更不触碰内核与磁盘布局，若发现用户态回归可单独回退 `user/libc/**` 改动而不影响内核与启动；libc smoke 默认关闭，关闭时与现状一致。

## Open Questions

- 无未决项。原三条开放问题已定：`freopen` 纳入本期（决策 4）、`setvbuf`/`setbuf` 暴露完整三模式（决策 4b）、标准 freestanding 头直接复用工具链并把 `sys/types.h` 转引 `<stddef.h>`（决策 7）。
