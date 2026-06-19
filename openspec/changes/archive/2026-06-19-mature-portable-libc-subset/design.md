## Context

BigOS 当前用户态已经有静态 ELF64 程序、`crt0`、bounded libc foundation、shell、packaged `/bin/*` 工具和默认关闭的 userland smoke。已归档的 libc foundation 覆盖了 errno、wrapper、字符串/内存、基础 stdlib、bounded stdio、`calloc`/`realloc`、`strtol`、`snprintf` 和 `DIR*` 风格目录枚举等基础能力。

这个 change 不是重做最小 libc，而是把现有基础推进到更适合“可移植小程序”的成熟子集。目标消费者是静态链接、freestanding-safe 的小型 C 程序：它们通常期望常见标准头文件、`ctype`、`time.h`、`assert.h`、无符号数值解析、稳定错误文本、stdio 的常见整数/宽度格式、文件/目录 wrapper 和清晰的失败语义。该工作影响 `user` 顶层用户态运行时和 libc、用户程序构建/打包、packaged tools、userland smoke 与文档，不改变内核 boot/runtime 边界。

本 change 不跨越 boot、IRQ、内核内存管理或驱动边界。`int 0x80` syscall ABI、syscall vector、ELF 装载模型、页表布局、磁盘镜像布局和 x86_64 Legacy BIOS 默认路径保持不变。

## Goals / Non-Goals

**Goals:**

- 定义 portable libc subset：面向可移植小程序的 bounded C library maturity layer。
- 收敛 public headers，使标准 C 子集、POSIX-like bounded wrapper、BigOS-specific helper 和 internal-only helper 的边界清晰。
- 补齐高频、低系统牵引的 libc helper：`ctype`、`time.h`、`assert.h`、无符号转换、错误文本、第一批无隐藏状态 search helper，以及 formatter 常见整数/宽度行为。
- 让 packaged tools 和代表性小程序优先通过 libc public API 消费现有进程、文件、目录、time/identity、wait、pipe/dup、cwd 和错误报告能力。
- 建立分层验证：头文件可构建、静态链接、用户态 smoke、代表性小程序组合行为、文档边界检查和环境不可用时的跳过记录。

**Non-Goals:**

- 不实现完整 POSIX libc、完整 hosted libc、动态链接、共享库、动态 loader、TLS、locale、线程、宽字符或完整浮点 formatting。
- 不实现完整 `FILE` 流、`fopen`/`fclose`/`fread`/`fwrite`/`fflush`、完整 buffering、完整 precision/flags 或完整 scanf family。
- 不扩大为完整 POSIX process model、完整 shell grammar、job control、terminal process group、termios、权限模型、symlink、mount namespace 或 `openat` family。
- 不引入 broad file-backed `mmap`、async I/O、SMP、UEFI runtime parity、新 ISA 或新 storage/device backend。
- 不修改 boot handoff、ELF 装载地址、linker 地址、page-table self mapping、interrupt vector、syscall vector、磁盘布局或内核 ABI。

## Decisions

### Decision: 按“可移植小程序常见依赖”选择接口

本 change 的接口选择以常见小型 C 程序的实际移植摩擦为准，而不是按 POSIX/ISO C 完整目录补齐。第一批成熟化应覆盖 `ctype.h` 常用分类/转换、`time.h` 中映射到现有 bounded time primitive 的类型和 helper、`assert.h` 的 freestanding 断言宏边界、`strtoul`/`strtoull`、`errno` 错误文本、formatter 常用整数/宽度行为、第一批无隐藏状态 search helper（`strchr`、`strrchr`、`strstr`、`memchr`），以及已有文件/目录/进程 wrapper 的头文件一致性。

备选方案是按完整 libc checklist 推进。该方案会立即牵出 locale、线程、wide char、完整 stdio、浮点 formatting、环境数据库和复杂 filesystem/process 语义，超出当前 bounded userland 目标，因此不采用。

### Decision: 第一批 search helper 只纳入无隐藏状态集合

第一批 tokenization/search/sort helper 收敛为 search-only helper：`strchr`、`strrchr`、`strstr` 和 `memchr`。这些接口无隐藏全局状态、不要求 comparator ABI、不改变调用者缓冲区，适合 portable small program 的常见字符/内存查找需求。

`strtok`、`qsort` 和 `bsearch` 不作为本 change 的必选交付项。`strtok` 具有隐藏状态且会修改输入缓冲区，容易引入后续线程/重入语义负担；`qsort` 和 `bsearch` 需要承诺 comparator 调用约定、元素访问边界和排序/搜索细节，只有在 packaged tools 或 representative programs 出现直接消费者时再纳入独立后续范围。

备选方案是一次性补齐 tokenization/search/sort 常见函数。该方案会扩大 public libc surface，并让当前没有消费者的接口过早变成 ABI 承诺，因此不采用。

### Decision: 头文件先定义兼容边界，再补实现

实现前先审查 public headers、umbrella header 和 packaged tools include 关系。每个 public declaration 必须归入标准 C 子集、POSIX-like bounded wrapper、BigOS-specific public helper、compatibility export 或 internal-only helper。未实现或未规格化的 hosted/POSIX 接口不得为了“看起来兼容”而声明。

备选方案是先添加函数，再整理 headers。该方案会扩大无规格 ABI 面，容易让小程序编译通过但运行行为不确定，因此不采用。

### Decision: `time.h` 和 `assert.h` 纳入本次 public header 成熟化

`time.h` 应只公开 BigOS 当前 bounded time primitive 能支撑的类型、常量和 helper，不声明 timezone、locale、calendar conversion、sleep/timer facility 或完整 POSIX time API。`assert.h` 应提供 freestanding-safe 的 `assert` 宏边界：`NDEBUG` 可禁用断言，断言失败走确定性诊断/终止路径，但不依赖宿主 stderr、信号、动态初始化或完整 hosted abort 语义。

备选方案是继续把 `time.h` 和 `assert.h` 留作 open question。用户已确认本次需要纳入，因此将其作为明确交付范围，但用 bounded 语义限制系统牵引。

### Decision: stdio 继续共享 bounded formatter

`printf`、`fprintf(stderr, ...)`、`snprintf` 和错误报告路径应共享同一套 bounded formatter。新增格式优先覆盖整数、指针、`size_t`、简单宽度和截断返回语义；不引入浮点、locale、wide char、完整 precision/flags 或完整 `FILE` 流。

备选方案是为 `snprintf` 或工具输出单独实现 formatter。该方案会导致行为分叉，后续难以验证不同输出 API 的一致性，因此不采用。

### Decision: POSIX-like wrapper 只作为现有内核能力的用户态消费层

libc 成熟化不得要求新增内核 syscall 作为前提。wrapper 应继续复用现有 `int 0x80` ABI、统一 errno 来源和已有 bounded process/fd/VFS/time/signal 能力。若小程序需要的行为超出内核现有能力，应记录为非目标或后续 change，而不是在 libc 中模拟完整 POSIX。

备选方案是在 libc 层做更强的 namespace、path canonicalization 或 process 状态模拟。该方案会和内核/VFS 事实产生漂移，且隐藏当前 bounded subset 的真实边界，因此不采用。

### Decision: 代表性程序验证优先于外部兼容测试套件

验证应使用仓库内可控的代表性小程序和 userland smoke，覆盖头文件、静态链接、参数/环境、数值解析、ctype、stdio/stderr、errno、文件/目录和 wrapper 失败路径。外部 libc/POSIX 测试套件可以作为未来参考，但不能作为本 change 的完成标准。

备选方案是直接引入大型兼容测试。该方案会包含大量当前明确非目标，产生噪声并推动范围失控，因此不采用。

## Risks / Trade-offs

- Scope creep 到完整 libc -> 用 specs 明确非目标，新增声明必须有实现、规格和验证。
- 头文件过度承诺 -> 先做 include/public declaration audit，未实现接口不得出现在 public support 面。
- formatter 行为分叉 -> 所有输出 API 复用 bounded formatter，并用 smoke 覆盖共同格式。
- `ctype`/转换函数受 locale 预期影响 -> 明确只支持 ASCII/C locale 风格的有界行为，不声明 locale。
- POSIX-like wrapper 被误解为完整 POSIX -> 文档和规格持续使用 bounded subset 表述。
- 运行时验证依赖环境 -> 任务要求记录 QEMU/Bochs/cross toolchain/镜像不可用时的跳过原因、替代检查和残余风险。

## Migration Plan

1. 盘点当前用户态 public headers、umbrella header、BigOS-specific helper、packaged tools 和 smoke 的 include/API 使用。
2. 按 portable libc subset 分类整理声明，移除或隐藏未实现/未规格化的 hosted/POSIX 声明。
3. 补齐高频 libc helper 与 wrapper 一致性，保持 errno、失败哨兵和已有 ABI 不变。
4. 迁移代表性 packaged tools 或新增小程序，使其通过 public libc API 覆盖 portable subset。
5. 扩展 userland/libc smoke 与构建验证，记录工具链或 emulator 不可用时的替代检查。
6. 同步 docs/en 与 docs/zh 用户态运行时边界说明。
7. 若某个新增接口破坏已有 userland，优先回退为 compatibility export 或从本 change 移出，并记录后续独立 change。

## Open Questions

无。
