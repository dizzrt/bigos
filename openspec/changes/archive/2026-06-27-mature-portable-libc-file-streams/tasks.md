## 1. 边界盘点与接口设计

- [x] 1.1 盘点既有用户态 stdio（`user/libc/stdio.c` 的 `stream_fd` 标准流模型与 `format_sink` 共享 formatter）、`user/libc/include/stdio.h` 当前声明与非支持注释，确认 `FILE` 升级为缓冲流的接入点与对既有 `printf`/`fprintf`/`puts`/`putchar`/`perror`/`snprintf` 调用者的兼容边界。
- [x] 1.2 盘点既有 fd/IO wrapper（`open`/`read`/`write`/`close`/`lseek`/`fsync`）与 `fcntl.h` flags、`unistd.h` `SEEK_*` 常量、`sys/types.h`（`size_t`/`off_t`/`ssize_t`/`NULL`），确认缓冲流后端复用边界与 `fopen` 模式到 `open` flags 的映射表。
- [x] 1.3 盘点既有 `user/libc/string.c`、`user/libc/ctype.c`、`user/libc/malloc.c` 与对应头，列出本期要补齐的 helper 与已有项，确认 `strtoll`/`strtoull` 复用 `strtol`/`strtoul` 解析逻辑的接入点，并确认未纳入项（隐藏全局态 `strtok` 等）保持非支持。
- [x] 1.4 确认 BigOS 用户构建的 include/`-ffreestanding`/`-nostdlib` 配置（`xmake/user_package.lua`，已确认不带 `-nostdinc`，工具链 `include/`+`include-fixed/` 在搜索路径中），核对工具链 freestanding 头（`stddef.h`/`stdint.h`/`stdbool.h`/`stdarg.h` 在 `include/`、`limits.h` 在 `include-fixed/`）可直接解析，并确认 `user/libc/include/sys/types.h` 的 `size_t`/`NULL` 与工具链 `<stddef.h>` 的重复定义冲突点。
- [x] 1.5 审查并确认本变更不改动 syscall number/ABI、启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector 与 CR3 切换；缓冲全部在用户态实现。

## 2. 缓冲 FILE 流核心实现

- [x] 2.1 在 `user/libc/include/stdio.h` 定义有界 `FILE` 结构与公开声明（`fopen`/`freopen`/`fclose`、`fread`/`fwrite`、`fgetc`/`getc`/`fgets`/`fputc`/`putc`/`fputs`/`ungetc`、`setvbuf`/`setbuf`、`fflush`/`feof`/`ferror`/`clearerr`/`fileno`、`fseek`/`ftell`/`rewind`、`EOF`/`BUFSIZ`/`_IOFBF`/`_IOLBF`/`_IONBF` 等必要宏），保持头最小化并更新非支持注释（不声称 `scanf` 家族、宽流、locale、`tmpfile`/`fmemopen`、完整 `fpos_t`）。
- [x] 2.2 在 `user/libc/stdio.c` 实现 `FILE` 对象与缓冲模型（fd、默认 `BUFSIZ` 容量缓冲、读/写游标、缓冲模式、`eof`/`error` 标志、单字节 `ungetc` 槽、读写方向标记、“缓冲区是否 libc 自有”标记），默认缓冲经 `malloc` 分配。
- [x] 2.3 实现 `fopen`（模式串→`open` flags 映射，文本/二进制行为一致、不做换行转换）、`freopen`（先 flush+close 旧 fd 再以同对象重定向，`path==NULL` 返回 `EINVAL`，失败置流为不可用态）与 `fclose`（先 `fflush` 再 `close`，仅释放 libc 自有缓冲与对象），覆盖未识别模式串 `EINVAL`、`open` 失败、分配失败的确定性失败与资源回滚（不泄漏 fd/缓冲）。
- [x] 2.4 实现缓冲读路径（`fread`/`fgetc`/`getc`/`fgets`/`ungetc`）：缓冲为空时单次 `read` 填充、`ungetc` 优先消费、`fgets` 保留换行并 NUL 终止、文件末尾/读错误设置 `eof`/`error`。
- [x] 2.5 实现缓冲写路径（`fwrite`/`fputc`/`putc`/`fputs`），并把既有 `putchar`/`puts`/`printf`/`fprintf` 重接到流缓冲出口，复用 `format_sink` 共享 formatter；按缓冲策略（满/行缓冲遇换行/方向切换/`fflush`/`fclose`）经 `write` 落盘。
- [x] 2.6 实现缓冲控制 `setvbuf`/`setbuf`：支持 `_IOFBF`/`_IOLBF`/`_IONBF`、调用方缓冲区与自定义容量（`buf==NULL` 时 `malloc`，并标记非自有/自有），要求首次 I/O 前调用否则失败；`setbuf` 作为薄封装。
- [x] 2.7 实现流状态与刷新（`fflush`/`feof`/`ferror`/`clearerr`/`fileno`）与有界定位（`fseek`/`ftell`/`rewind`）：定位先刷新写缓冲、丢弃/重对齐读缓冲与 `ungetc` 槽再 `lseek`，`ftell` 返回经缓冲修正的逻辑偏移。
- [x] 2.8 把 `stdin`/`stdout`/`stderr` 迁移为绑定 fd 0/1/2 的静态 `FILE`（`stderr` 无缓冲、`stdout` 行缓冲、`stdin` 读缓冲），并在 libc 退出收敛路径（`exit`/`user/crt0` 返回点或 `user/libc` 退出 hook）刷新所有可刷新流；确认既有把 `stdout`/`stderr` 当不透明指针的代码无需改动。

## 3. 扩充 helper 与标准头

- [x] 3.1 在 `user/libc/string.c`/`string.h` 补齐受支持的 `memcmp`、`strcat`/`strncat`、`strspn`/`strcspn`/`strpbrk`、显式可重入 `strtok_r`（调用方 `saveptr`、无隐藏全局态），按标准 C 语义实现并更新头声明与非支持注释。
- [x] 3.2 在 `user/libc/stdlib.c`（或既有实现文件）补齐 `abs`/`labs`、`strtoll`/`strtoull`（复用既有 base/end 指针/errno/溢出契约扩展到 `long long`）、使用显式比较器的 `qsort`/`bsearch`（无 locale），并更新 `stdlib.h` 声明。
- [x] 3.3 在 `user/libc/ctype.c`/`ctype.h` 补齐 `isxdigit`/`ispunct`/`iscntrl`/`isgraph`/`isblank`，保持确定性 ASCII/C-locale-style 行为，覆盖 unsigned char 取值与 EOF-style 输入。
- [x] 3.4 标准 freestanding 头：直接复用工具链 `-ffreestanding` 版本，本仓库 `user/libc/include` 不提供 `stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h` 副本；把 `user/libc/include/sys/types.h` 的 `size_t`/`NULL` 改为 `#include <stddef.h>` 转引（仅保留 `ssize_t`/`off_t`/`mode_t`/`pid_t`），并核对 `libc.h` 伞头与既有用户程序无重复定义冲突。

## 4. 验证程序与默认关闭 smoke

- [x] 4.1 新增/扩展代表性可移植小程序（如扩展 `user/smoke/bin/libc_subset.c` 或新增 FILE 流专用 smoke 源），覆盖 `fopen`/`freopen`+`fread`/`fwrite` 回环、`fgets` 按行读取、`fseek`/`ftell`/`rewind` 定位、`setvbuf` 模式切换与调用方栈缓冲、`fflush`/缓冲落盘、`feof`/`ferror`/`clearerr` 状态、退出路径刷新（写后不 `fclose` 直接退出仍落盘）、`freopen(stdout)` 后输出落到新文件且指针不变。
- [x] 4.2 覆盖确定性失败路径：未识别 `fopen` 模式 `EINVAL`、`freopen` 的 `path==NULL` `EINVAL` 与打开失败置流不可用、不存在文件 `ENOENT`、对 NULL/已关闭流操作、`setvbuf` 在已有 I/O 后失败、底层错误经流 `error` 标志与 errno 翻译可观察、分配失败（如超大请求）回滚不泄漏。
- [x] 4.3 覆盖扩充 helper 行为：`memcmp`/`strcat`/`strncat`/`strspn`/`strcspn`/`strpbrk`/`strtok_r`、`abs`/`labs`/`strtoll`/`strtoull`/`qsort`/`bsearch`、扩充 ctype 分类，以及“只 include 工具链标准 freestanding 头 + `sys/types.h`”的最小编译用例（固化无重复定义冲突的回归）。
- [x] 4.4 在 `xmake.lua`/`xmake/user_package.lua` 接入默认关闭的 libc 流验证 smoke 开关与确定性 marker（如 `BIGOS_LIBC_FILE_STREAM_PASSED`/`FAILED`），打包代表性程序；确认默认关闭时默认 `/bin/*` 静态集合、默认磁盘镜像与默认启动不变。

## 5. 构建、运行与文档收尾

- [x] 5.1 运行 xmake 目标构建（`x86_64-elf-gcc`/`as`/`ld`），smoke 开/关两种配置；确认用户态 libc/头改动编译链接通过、代表性程序仍为 bounded 静态 ELF64 `ET_EXEC`，若交叉工具链/构建环境不可用则记录 blocker、替代检查与剩余风险。
- [x] 5.2 跑默认关闭 libc 流 smoke（优先 QEMU `--display none` headless，serial marker 校验 `BIGOS_LIBC_FILE_STREAM_PASSED`），并跑默认启动回归确认关闭时正常进入 shell、运行静态 `/bin/*`、PID-1 init 不变；可用时跑 Bochs Legacy BIOS 交叉验证，环境不可用时记录跳过原因与剩余风险。
- [x] 5.3 记录本变更为 C-only 用户态改动：不涉及内核 C++ 源/头/KTL/C++ 构建配置，clang/clangd C++ 辅助检查不适用（按 OpenSpec rules 显式记录“不适用”而非跳过）；若实际触及任何 C++ 则补做对应 clang/clangd 检查并区分历史/当前/false positive 诊断。
- [x] 5.4 若改动 `tools/bigosdev` 打包或新增/修改 Python 验证辅助，使用 `uv run ...` 并补 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`（含 source-count/契约测试更新）；若未改 Python，记录不适用。
- [x] 5.5 更新 docs/en 与 docs/zh 镜像的用户态运行时文档，描述有界缓冲 `FILE` 流子集与扩充 helper 边界（不声称完整 hosted stdio、`scanf` 家族、locale、宽字符、浮点、完整 `fpos_t`、动态链接），保持目录结构同构与仓库相对路径。
- [x] 5.6 更新 `roadmap.md` 中 M12.2 完成状态（保持 roadmap 仅项目规划级描述，不加入入口点、命令、marker、文件路径或源码细节）。
- [x] 5.7 运行 `openspec validate mature-portable-libc-file-streams --strict` 与 `openspec status`，确认 artifacts/规格/任务处于可归档状态；在验证记录中区分已通过、无法运行（含原因与剩余风险）、历史诊断与当前变更新问题。

## 验证记录

### 已通过

- 用户态 libc/头构建（C-only）：交叉工具链 `x86_64-elf-gcc 12.2.0` 下，`xmake clean` 后默认配置（smoke 关）与 `--libc_file_stream_smoke=y` 配置均 `build ok`；所有 libc 源（`syscall.c`/`string.c`/`malloc.c`/`stdio.c`/`env.c`/`ctype.c`/`assert.c`）与 `crt0.s`、新增 `user/smoke/libc_file_stream_smoke.c`、扩展后的 `user/smoke/bin/libc_subset.c` 编译通过。
- 代表性程序产物边界：`libc_file_stream_smoke` 链接为 32008 字节静态 `ET_EXEC` ELF64（< 64 KiB 上界），`x86_64-elf-readelf -h` 确认 `Type: EXEC`、`Machine: AMD x86-64`。
- 默认关闭 libc 流 smoke：`--libc_file_stream_smoke=y` 经 QEMU headless（`--display none`）观察到 `BIGOS_LIBC_FILE_STREAM_PASSED`，日志仅含 `BIGOS_INIT_ENTER` 与 PASS，无 `FAILED`/`PANIC`（`logs/libc_file_stream.log`）。覆盖 fopen/freopen/fclose 回环、fseek/ftell/rewind、fgets、setvbuf（含调用方栈缓冲）、确定性失败路径、freopen(stdout) 重定向与退出刷新（子进程）、扩充 string/stdlib/ctype helper。
- 默认启动回归（smoke 关）：默认配置经 QEMU headless 观察到 `BIGOS_USER_EXEC`（`logs/default_boot.log`），确认正常进入 PID-1 init 与 `/bin/sh`、行缓冲 stdout 与 printf/fprintf 重接无回退。
- userland_smoke 交叉回归：`--userland_smoke=y` 经 QEMU headless（60s 超时）观察到 `BIGOS_USERLAND_PASSED` 与 `BIGOS_FILESYSTEM_MATURITY_PASSED`（`logs/userland_smoke.log`），确认 printf/fprintf/perror/puts 经新缓冲路径在完整 userland（含扩充后的 libc_subset 探针）无回退。
- Python 辅助（改动 `tools/bigosdev/core.py` 注册新 smoke case，并更新 `tests/`）：`uv run ruff check`、`uv run ruff format --check`、`uv run pyright tools/bigosdev/core.py`（0 errors）全部通过；`uv run pytest` 全量 326 passed，新增/更新的 `test_user_c_baseline_source.py`、`test_bigosdev.py`、`test_syscall_entry_source.py` 源契约断言通过。
- ABI 护栏：smoke 与 libc_subset 程序内 `_Static_assert` 固化 LP64（`size_t`/`long`/`long long`=8、`int`=4、`void*`=8、`INT_MAX`），并新增“仅 include 工具链 freestanding 头 + `sys/types.h` 无重复定义冲突”的最小编译回归。

### 无法运行 / 剩余风险

- Bochs Legacy BIOS 交叉验证未执行：本变更为纯用户态 C（libc/crt0/头/smoke/构建开关），不触及 boot/BIOS/实模式-保护模式-长模式切换、ATA PIO、IRQ/timer、port IO 或低层驱动；按仓库规则此类改动以 QEMU headless 为准，Bochs 交叉验证不适用。剩余风险低。
- clang/clangd C++ 辅助检查不适用：本变更不修改任何内核 C++ 源/头/KTL/C++ 构建配置（仅 C 源、C 头、汇编 crt0、Lua 构建开关、Python 辅助、文档）。按 OpenSpec rules 显式记录“不适用”，非跳过。
- 缓冲容量与内核 I/O 上限耦合：写按 `SYS_WRITE_MAX_LEN`(128) 分块（fd 1/2 console 快路径超限会 fault 杀进程）、读按 `SYS_IO_MAX_LEN`(512) 分块；若内核后续调整这些常量，用户态分块常量需同步复核。

### 历史诊断（与本变更无关，pre-existing）

- 全量 `uv run pytest` 在干净基线（stash 掉本变更后）已存在 19 项失败，全部读取内核 C++ 源（`kernel/core/proc/proc.cc`、`kernel/mm/vmem.cc`、fork/COW、scheduler、address-space 等）或无关 openspec 归档/布局断言，与本变更文件无交集；本变更引入 0 项新失败（应用后仍为同样 19 failed / 326 passed）。这些属于 `main` 上既有的进行中重构，不在本变更范围内。
