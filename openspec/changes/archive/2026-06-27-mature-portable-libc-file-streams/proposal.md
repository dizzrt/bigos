## Why

BigOS 当前的 bounded libc 子集已经覆盖 freestanding-safe 头文件、ctype、bounded 数值转换、共享 formatter、无状态 search helper 与 POSIX-like wrapper 消费面，但其 `stdio.h` 仍只把 `stdin`/`stdout`/`stderr` 当作 fd 0/1/2 的不透明句柄，显式不提供 `fopen`/`fclose`/`fread`/`fwrite`/`fgets`/缓冲与文件定位语义。绝大多数“可移植标准小程序”默认依赖缓冲 `FILE` 流（按文件名打开、按行/块读写、`feof`/`ferror` 状态、`fseek`/`ftell` 定位），因此现状仍把这些程序挡在门外，必须改写为裸 fd 调用才能在 BigOS 上构建。

本变更面向更完整的 libc 子集：在有界 freestanding-safe 语义内，把 `FILE` 从“fd 别名”升级为“真正的缓冲流抽象”，并补齐 portable 小程序高频依赖的 `string.h`/`stdlib.h`/`ctype.h` helper 与标准 freestanding 头（如 `stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h`），使更多标准小程序可以原样静态链接运行。它明确不声称完整 hosted stdio、完整 POSIX libc、locale、宽字符、浮点格式化或 `mmap` 支持的 `FILE`。

## What Changes

- 把用户态 `stdio.h`/`stdio.c` 的 `FILE` 由 fd 0/1/2 不透明别名升级为有界缓冲流对象，新增按文件名打开/关闭/重定向的 `fopen`/`freopen`/`fclose`，并保留既有 `stdin`/`stdout`/`stderr` 标准流语义。
- 新增有界缓冲读写接口：`fread`/`fwrite`/`fgetc`/`getc`/`fgets`/`fputc`/`putc`/`fputs`/`ungetc`(单字节回推)，并把既有 `putchar`/`puts`/`printf`/`fprintf` 重接到流缓冲路径之上，复用现有共享 formatter。
- 新增缓冲控制接口 `setvbuf`/`setbuf`：支持 `_IOFBF`/`_IOLBF`/`_IONBF` 三种模式、调用方提供的缓冲区与自定义容量（`buf == NULL` 时由 libc 分配），须在打开流后、首次 I/O 前调用。
- 新增有界流状态与定位接口：`fflush`、`feof`、`ferror`、`clearerr`、`fileno`，以及基于既有 `lseek` 的 `fseek`/`ftell`/`rewind`（字节偏移子集，不声称文本流转换或完整 `fpos_t`/`fgetpos`/`fsetpos`）。
- 补齐 portable 小程序高频 `string.h`/`stdlib.h`/`ctype.h` helper：例如 `memcmp`、`strcat`/`strncat`、`strcspn`/`strspn`/`strpbrk`、`strtok_r`（显式无隐藏全局态的可重入版本）；`abs`/`labs`、`strtoll`/`strtoull`、`qsort`/`bsearch`（显式比较器、无 locale）；以及 `isxdigit`/`ispunct`/`iscntrl`/`isgraph`/`isblank` 等分类 helper。补齐内容以实际新增消费者或验证程序为准，未纳入项保持显式非支持边界。
- 复用工具链 freestanding 标准头子集：用户态可移植小程序直接 `#include` 交叉工具链在 `-ffreestanding` 下提供的 `stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h`，本仓库 `user/libc/include` 不再为这些标准头提供副本；并把 `user/libc/include/sys/types.h` 的 `size_t`/`NULL` 改为转引 `<stddef.h>`，从根上避免与工具链头的重复定义冲突。
- 新增/扩展默认打包的代表性可移植小程序与默认关闭验证 smoke：覆盖 `fopen`/`freopen`+`fread`/`fwrite` 回环、`fgets` 按行读取、`fseek`/`ftell` 定位、`setvbuf` 模式切换、`fflush`/缓冲刷新、`feof`/`ferror` 状态、新增 string/stdlib helper 行为与确定性失败路径；默认启动仍走既有静态 `/bin/*` 与 PID-1 init。
- 不引入：完整 hosted stdio 中超出本子集的部分（宽流、临时文件 `tmpfile`/`tmpnam`、`fmemopen`/`open_memstream`、完整文本流换行转换、完整 `fpos_t`/`fgetpos`/`fsetpos`、`scanf` 家族（除非后续变更显式新增消费者））、locale/多字节/宽字符、浮点格式化与解析、信号可中断 I/O、线程安全流锁（`flockfile`）、file-backed `mmap`，以及动态链接/共享库化的 `libc.so`。

## Capabilities

### New Capabilities

- `bounded-file-streams`: 定义 BigOS 有界缓冲 `FILE` 流子集，覆盖 `FILE` 对象生命周期与缓冲模型、按文件名打开/重定向/关闭（`fopen`/`freopen`/`fclose`）、缓冲读写与单字节回推、缓冲控制（`setvbuf`/`setbuf` 三模式）、流状态（`feof`/`ferror`/`clearerr`）、有界字节定位（`fseek`/`ftell`/`rewind`）、刷新（`fflush`）与标准流（`stdin`/`stdout`/`stderr`）语义，以及失败行为、内存/fd 资源边界、非目标与默认关闭验证边界。

### Modified Capabilities

- `user-libc-min`: 既有需求把“complete `FILE` stream”列为不暴露边界；修改为允许在有界范围内暴露缓冲 `FILE` 流子集（`fopen`/`fread`/`fwrite`/`fgets` 等）与新增 string/stdlib/ctype helper 及标准 freestanding 头声明，同时保持对完整 hosted stdio、locale、宽字符、浮点、`scanf` 家族等的非支持边界。
- `portable-libc-subset`: 既有需求在多处声明“MUST NOT ... complete `FILE` streams / hosted stdio”，并把 formatter/stdio 限定为 fd-backed 子集；修改为把可移植子集扩展到有界缓冲 `FILE` 流与扩充的 helper 批次，并将边界表述从“无任何 FILE 流”调整为“有界缓冲 FILE 流子集，非完整 hosted stdio”。
- `user-program-build`: 既有需求要求代表性小程序通过 freestanding 静态 ELF64 路径验证 libc 子集；修改/重申为代表性程序可经该路径消费缓冲 `FILE` 流与新增 helper，仍不依赖 hosted libc、动态链接器或共享库。

## Impact

- 影响用户态 libc 实现与头文件（`user/libc/stdio.c` 的 `FILE` 模型与 formatter 接入、`user/libc/string.c`、`user/libc/ctype.c`、`user/libc/malloc.c`/新增排序查找、`user/libc/include/stdio.h`、`string.h`、`stdlib.h`、`ctype.h`，以及 `user/libc/include/sys/types.h` 改为转引工具链 `<stddef.h>` 与 `libc.h` 伞头）。本仓库不为 `stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h` 提供用户态副本，直接复用交叉工具链 freestanding 版本（已确认 `x86_64-elf-gcc` 在当前用户构建 flag 下可解析，`limits.h` 经 GCC `include-fixed` 提供）。
- 影响用户态消费者与验证：既有 `/bin/*` 小程序可逐步迁移到 `FILE` 流（非强制，保持裸 fd 路径可用）、新增/扩展代表性可移植程序与默认关闭 libc smoke 入口；与用户 ABI 共享的 errno/`SEEK_*` 常量复用既有定义。
- 复用既有内核 fd/VFS 读写/`lseek`/`fsync` syscall 与 `brk`-backed 分配作为缓冲流后端；本变更不新增 syscall number、不改变 syscall 语义，缓冲全部在用户态实现。
- 不改变启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或既有静态 `ET_EXEC` 装载 ABI；默认启动在 libc smoke 关闭时仍正常进入 shell 并运行静态 `/bin/*`。
- 架构假设为当前 x86_64 freestanding 内核（Legacy BIOS/MBR/exFAT 默认运行 backend，UEFI spike 非运行时 parity，单核基线即可）；内存仅经既有 `brk`/受限匿名映射/栈/静态缓冲获取，不引入 file-backed `mmap` 或 hosted allocator；工具链以 xmake + x86_64-elf GCC/binutils 为准，辅助 Python 验证通过 `uv run ...`。
- 非目标（OS 层面显式排除）：完整 hosted stdio、完整 POSIX libc、locale/宽字符/多字节、浮点格式化与解析、`scanf` 家族、`tmpfile`/`fmemopen`/`open_memstream`、线程安全流锁、信号可中断 I/O、SMP、动态链接/共享库化 libc、broad file-backed `mmap`、async I/O 与新 ISA/backend。
