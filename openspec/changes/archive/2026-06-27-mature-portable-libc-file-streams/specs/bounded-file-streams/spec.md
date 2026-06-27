## ADDED Requirements

### Requirement: 有界缓冲 FILE 流对象与生命周期

BigOS 用户态 libc SHALL 提供有界缓冲 `FILE` 流对象，作为用户态实现的缓冲抽象，底层复用既有 `open`/`read`/`write`/`close`/`lseek`/`fsync` syscall 与 `brk`-backed 分配。`FILE` 对象 MUST 至少承载底层 fd、固定容量字节缓冲、缓冲内有效长度与读/写游标、缓冲模式、`eof`/`error` 标志与单字节回推槽。该能力 MUST NOT 引入 hosted stdio runtime、locale、宽字符、浮点、线程安全流锁或 file-backed `mmap`。

#### Scenario: fopen 打开命名文件并分配缓冲

- **WHEN** 用户程序以受支持模式串调用 `fopen(path, mode)`
- **THEN** libc MUST 将模式串映射到既有 `open` flags 并打开底层 fd，分配 `FILE` 对象与缓冲
- **AND** 成功时返回非 NULL `FILE *`，失败时返回 NULL 并设置 `errno`

#### Scenario: fopen 失败回滚不泄漏资源

- **WHEN** `fopen` 在底层 `open` 失败或缓冲/对象分配失败
- **THEN** libc MUST 返回 NULL 并保留或设置 `errno`
- **AND** MUST NOT 泄漏已打开 fd 或已分配缓冲

#### Scenario: 未识别模式串确定性失败

- **WHEN** 用户程序以未识别或不支持的模式串调用 `fopen`
- **THEN** libc MUST 返回 NULL 并设置 `errno` 为 `EINVAL`
- **AND** MUST NOT 打开底层 fd 或分配缓冲

#### Scenario: fclose 刷新并释放

- **WHEN** 用户程序对一个打开的 `FILE` 调用 `fclose`
- **THEN** libc MUST 先刷新写缓冲，再关闭底层 fd，最后释放 libc 自有缓冲与对象
- **AND** 返回 0 表示成功，刷新或关闭失败时返回 `EOF` 并设置 `errno`

### Requirement: freopen 复用同一 FILE 对象重定向

BigOS 用户态 libc SHALL 提供 `freopen(path, mode, stream)`，在不更换调用方持有的 `FILE *` 的前提下把该流重定向到 `path` 指定的文件。`freopen` MUST 先刷新并关闭 `stream` 的旧底层 fd，再以 `mode` 走与 `fopen` 相同的 `open` 流程，并把新 fd 与重置后的缓冲/标志写回同一个 `FILE` 对象。`path` 为 NULL 的仅改模式变体 MUST NOT 在本子集支持。该接口 MUST NOT 引入 hosted stdio 全语义或宽流。

#### Scenario: freopen 重定向标准流且指针不变

- **WHEN** 用户程序调用 `freopen(path, "w", stdout)`
- **THEN** libc MUST 关闭 `stdout` 旧底层 fd 并把 `stdout` 重定向到新文件，返回同一个 `FILE *`
- **AND** 随后经 `printf`/`stdout` 的输出 MUST 落到新文件，且 `stdout` 全局指针保持不变

#### Scenario: freopen 打开失败置流为不可用

- **WHEN** `freopen` 在底层 `open` 失败
- **THEN** libc MUST 返回 NULL 并把 `stream` 置为已关闭/错误态
- **AND** MUST NOT 复用旧底层 fd 或泄漏资源

#### Scenario: freopen 拒绝仅改模式变体

- **WHEN** 用户程序以 `path == NULL` 调用 `freopen`
- **THEN** libc MUST 返回 NULL 并设置 `errno` 为 `EINVAL`
- **AND** MUST NOT 改变 `stream` 已有的底层 fd

### Requirement: setvbuf 与 setbuf 缓冲控制

BigOS 用户态 libc SHALL 提供 `setvbuf(stream, buf, mode, size)` 与 `setbuf(stream, buf)`，支持 `_IOFBF`（全缓冲）、`_IOLBF`（行缓冲）、`_IONBF`（无缓冲）三种模式。`setvbuf` MUST 在打开流之后、对该流首次 I/O 之前调用，否则返回非零失败。当 `buf` 非 NULL 时 libc MUST 使用调用方提供的缓冲区与 `size` 容量并记录该缓冲区为非 libc 自有；当 `buf` 为 NULL 时 libc MUST 按 `size` 自行分配（`size==0` 或 `_IONBF` 走无缓冲）。`fclose`/`freopen`/再次 `setvbuf` MUST 只释放 libc 自有缓冲。`setbuf` MUST 实现为 `setvbuf` 的薄封装。该接口 MUST NOT 依赖 hosted stdio runtime、locale 或线程安全流锁。

#### Scenario: setvbuf 切换无缓冲模式即时落盘

- **WHEN** 用户程序在首次 I/O 前对流调用 `setvbuf(stream, NULL, _IONBF, 0)`
- **THEN** 随后经该流的写 MUST 即时经底层 `write` 落盘而不累积缓冲
- **AND** 返回 0 表示成功

#### Scenario: setvbuf 使用调用方缓冲区不被 libc 释放

- **WHEN** 用户程序以非 NULL `buf` 与 `_IOFBF` 调用 `setvbuf` 并随后 `fclose`
- **THEN** libc MUST 使用该调用方缓冲区进行全缓冲
- **AND** `fclose`/`freopen`/再次 `setvbuf` MUST NOT 释放调用方提供的缓冲区

#### Scenario: setvbuf 在已有 I/O 后调用失败

- **WHEN** 用户程序在对流执行过 I/O 之后调用 `setvbuf`
- **THEN** libc MUST 返回非零失败
- **AND** MUST NOT 在已有缓冲数据的情况下切换缓冲区导致状态不一致

### Requirement: 受支持的有界 fopen 模式子集

BigOS 用户态 libc SHALL 支持有界 `fopen` 模式子集，至少包括 `"r"`、`"w"`、`"a"`、`"r+"`、`"w+"`、`"a+"` 及其带 `"b"` 变体，并映射到既有内核 `open` flags 语义。`"b"`（二进制）与不带 `"b"`（文本）在 BigOS 行为 MUST 相同，即不做换行转换。该子集 MUST NOT 声称完整 hosted `fopen` 模式语义、文本流换行转换或临时文件创建。

#### Scenario: 读/写/追加模式映射正确

- **WHEN** 用户程序以 `"r"`/`"w"`/`"a"`/`"r+"`/`"w+"`/`"a+"` 之一打开文件
- **THEN** libc MUST 使用对应的 `O_RDONLY`/`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`/`O_APPEND` 组合调用既有 `open`
- **AND** 由此产生的可读/可写/截断/追加行为 MUST 与所选模式一致

#### Scenario: 文本与二进制行为一致

- **WHEN** 用户程序以带 `"b"` 与不带 `"b"` 的等价模式打开同一文件并执行相同读写
- **THEN** libc MUST 产生相同的字节级结果
- **AND** 文档与头注释 MUST 声明 BigOS 不做文本/二进制换行转换

### Requirement: 缓冲读接口

BigOS 用户态 libc SHALL 提供有界缓冲读接口，至少包括 `fread`、`fgetc`/`getc`、`fgets` 与单字节 `ungetc`。读路径 MUST 在缓冲为空时经一次底层 `read` 填充缓冲，再从缓冲拷贝并推进游标；`ungetc` MUST 优先于底层数据被后续读消费。读接口 MUST NOT 依赖 hosted stdio、locale 或宽字符。

#### Scenario: fread 按元素读取并报告计数

- **WHEN** 用户程序调用 `fread(ptr, size, nmemb, stream)`
- **THEN** libc MUST 经缓冲读取至多 `size*nmemb` 字节并返回完整读取的元素个数
- **AND** 到达文件末尾或读错误时返回的计数 MUST 反映实际读取，并相应设置 `eof` 或 `error` 标志

#### Scenario: fgets 按行读取并 NUL 终止

- **WHEN** 用户程序调用 `fgets(buf, n, stream)`
- **THEN** libc MUST 读取至多 `n-1` 个字节，遇换行符停止并保留换行，且对 `buf` 做 NUL 终止
- **AND** 在读取任何字节前即到达文件末尾时 MUST 返回 NULL 并设置 `eof` 标志

#### Scenario: ungetc 回推单字节

- **WHEN** 用户程序调用 `ungetc(c, stream)` 回推一个非 `EOF` 字节
- **THEN** 下一次读操作 MUST 先返回该回推字节
- **AND** 在缓冲流语义内至少支持单字节回推，超过支持深度时返回 `EOF`

### Requirement: 缓冲写接口

BigOS 用户态 libc SHALL 提供有界缓冲写接口，至少包括 `fwrite`、`fputc`/`putc`、`fputs`，并将既有 `putchar`/`puts`/`printf`/`fprintf` 重接到缓冲流出口之上，复用既有共享 formatter。写路径 MUST 在缓冲满、行缓冲遇换行、`fflush`、读写方向切换或 `fclose` 时经底层 `write` 落盘。写接口 MUST NOT 依赖 hosted stdio、locale、宽字符或浮点格式化。

#### Scenario: fwrite 缓冲累积并按策略落盘

- **WHEN** 用户程序调用 `fwrite(ptr, size, nmemb, stream)`
- **THEN** libc MUST 将数据写入流缓冲并按缓冲策略在适当时机经底层 `write` 落盘
- **AND** 返回完整写入的元素个数，写错误时返回的计数 MUST 反映实际写入并设置 `error` 标志

#### Scenario: printf 系列经缓冲流输出

- **WHEN** 用户程序调用 `printf`、`fprintf`、`putchar`、`puts` 或 `fputs`
- **THEN** libc MUST 经流缓冲与共享 formatter 产生输出，最终经底层 fd 的 `write` 可观察
- **AND** 受支持格式行为 MUST 与既有 fd-backed formatter 在相同 destination 上保持一致

### Requirement: 流状态与刷新

BigOS 用户态 libc SHALL 提供有界流状态与刷新接口，至少包括 `fflush`、`feof`、`ferror`、`clearerr` 与 `fileno`。`fflush` MUST 将写缓冲经底层 `write` 落盘；`feof`/`ferror` MUST 反映流的文件末尾/错误标志；`clearerr` MUST 清除这些标志；`fileno` MUST 返回底层 fd。该接口 MUST NOT 依赖 hosted stdio 或线程安全流锁。

#### Scenario: fflush 落盘写缓冲

- **WHEN** 用户程序在写入后调用 `fflush(stream)`
- **THEN** libc MUST 将当前写缓冲经底层 `write` 落盘
- **AND** 落盘失败时返回 `EOF` 并设置 `error` 标志与 `errno`

#### Scenario: feof 与 ferror 反映流状态

- **WHEN** 流遇到文件末尾或读写错误后用户程序调用 `feof`/`ferror`
- **THEN** 相应函数 MUST 返回非零表示该状态
- **AND** `clearerr` 调用后 `feof`/`ferror` MUST 返回零

#### Scenario: fileno 返回底层 fd

- **WHEN** 用户程序对一个打开的 `FILE` 调用 `fileno`
- **THEN** libc MUST 返回该流绑定的底层 fd
- **AND** 该 fd MUST 与 `fopen`/标准流绑定的 fd 一致

### Requirement: 有界字节定位

BigOS 用户态 libc SHALL 提供基于既有 `lseek` 的有界字节定位接口 `fseek`、`ftell` 与 `rewind`，使用 `SEEK_SET`/`SEEK_CUR`/`SEEK_END` 既有 whence 常量。定位 MUST 先刷新写缓冲、丢弃或重对齐读缓冲与 `ungetc` 槽，再调整底层偏移；`ftell` MUST 返回底层偏移经缓冲内未消费/未刷新字节修正后的逻辑位置。该接口 MUST NOT 声称完整 `fpos_t`/`fgetpos`/`fsetpos` 或文本流定位转换语义。

#### Scenario: fseek 刷新并重对齐缓冲

- **WHEN** 用户程序调用 `fseek(stream, offset, whence)`
- **THEN** libc MUST 先刷新写缓冲并丢弃/重对齐读缓冲与回推槽，再经 `lseek` 调整底层偏移
- **AND** 成功返回 0，底层 `lseek` 失败时返回 -1 并设置 `errno`

#### Scenario: ftell 报告逻辑位置

- **WHEN** 用户程序在缓冲内有未消费读数据或未刷新写数据时调用 `ftell`
- **THEN** libc MUST 返回经缓冲修正后的逻辑字节偏移
- **AND** 该值 MUST 与随后 `fseek(SEEK_SET)` 回到同一位置的语义一致

#### Scenario: rewind 回到起点并清错误

- **WHEN** 用户程序调用 `rewind(stream)`
- **THEN** libc MUST 等价于 `fseek(stream, 0, SEEK_SET)` 并清除 `error` 标志
- **AND** 随后读写 MUST 从文件起点开始

### Requirement: 标准流缓冲语义

BigOS 用户态 libc SHALL 将 `stdin`、`stdout`、`stderr` 实现为绑定 fd 0/1/2 的静态 `FILE` 流对象。`stderr` MUST 为无缓冲以保证错误即时可见；`stdout` MUST 采用行缓冲或在进程退出/刷新路径保证完整落盘的等价策略；标准流 MUST NOT 参与动态分配。libc 退出路径（`exit` 或从 `main` 收敛）MUST 刷新所有可刷新流，避免写缓冲数据丢失。

#### Scenario: 标准流绑定固定 fd

- **WHEN** 用户程序经 `stdin`/`stdout`/`stderr` 执行读写
- **THEN** 这些流 MUST 分别绑定 fd 0/1/2 并经既有 fd/VFS/console 路径可观察
- **AND** 既有把 `stdout`/`stderr` 传入 `fprintf`/`putchar`/`puts` 的程序 MUST 无需改动即可继续工作

#### Scenario: stderr 无缓冲即时可见

- **WHEN** 用户程序经 `stderr` 输出错误文本且未显式 `fflush`
- **THEN** 输出 MUST 即时经 fd 2 可观察
- **AND** MUST NOT 因缓冲而延迟到后续刷新

#### Scenario: 退出路径刷新流

- **WHEN** 用户程序在 `fwrite`/`printf` 后未显式 `fclose`/`fflush` 即调用 `exit` 或从 `main` 返回
- **THEN** libc MUST 在退出收敛路径刷新所有可刷新写缓冲
- **AND** 已缓冲的写数据 MUST 经底层 `write` 落盘而不丢失

### Requirement: 缓冲流失败行为确定

BigOS 用户态 libc SHALL 为缓冲流的失败与边界条件提供确定性行为。对 NULL 流、已关闭流的二次操作 MUST 走确定性失败（返回错误哨兵并按约定设置标志/errno）而非未定义控制流。底层 `read`/`write`/`lseek`/`close` 返回错误时 MUST 设置流 `error` 标志并翻译 `errno`。该能力 MUST NOT 通过吞掉内核错误或伪造成功来掩盖底层失败。

#### Scenario: 对无效流确定性失败

- **WHEN** 用户程序对 NULL 或已关闭的 `FILE` 调用流接口
- **THEN** libc MUST 返回接口约定的失败哨兵并按约定设置 `errno`
- **AND** MUST NOT 触发未定义控制流、二次 `close` 同一 fd 或越界访问

#### Scenario: 底层错误经流状态可观察

- **WHEN** 底层 `read`/`write`/`lseek` 返回负 errno
- **THEN** libc MUST 设置流 `error` 标志并把内核负 errno 翻译为用户态正 errno
- **AND** MUST NOT 把底层失败伪造为成功返回

### Requirement: 缓冲流验证

BigOS SHALL 通过代表性可移植小程序与默认关闭 smoke 验证有界缓冲 `FILE` 流行为。验证 MUST 覆盖 `fopen`/`freopen`+`fread`/`fwrite` 回环、`fgets` 按行读取、`fseek`/`ftell`/`rewind` 定位、`setvbuf` 模式切换与调用方缓冲区、`fflush`/缓冲落盘、`feof`/`ferror`/`clearerr` 状态、退出路径刷新与至少一条确定性失败路径。环境依赖的运行时验证 MAY 仅在显式记录缺失工具/配置与剩余风险时跳过。

#### Scenario: 运行时验证观察缓冲流回环

- **WHEN** 缓冲流验证在配置好的 emulator 环境运行
- **THEN** 验证 MUST 观察至少一个代表性程序完成 `fopen`→写→定位→读回→校验的回环
- **AND** 结果 MUST 可由用户可观察输出、退出状态或确定性串口/日志信号判定

#### Scenario: 环境不可用时记录跳过

- **WHEN** x86_64 交叉工具链、xmake、QEMU、Bochs、显示/ROM 依赖或磁盘镜像配置不可用
- **THEN** 对应验证 MAY 被跳过
- **AND** 验证记录 MUST 标明缺失条件、已运行的替代检查与剩余风险
