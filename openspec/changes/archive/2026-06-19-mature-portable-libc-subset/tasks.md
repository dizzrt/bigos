## 1. 接口盘点与范围确认

- [x] 1.1 盘点 `user` 下 libc public headers、umbrella header、BigOS-specific helper、raw syscall helper、packaged `/bin/*` 和 smoke 程序的 include/API 使用关系。
- [x] 1.2 将每个 public declaration 分类为标准 C 子集、POSIX-like bounded wrapper、BigOS-specific public helper、compatibility export 或 internal-only helper。
- [x] 1.3 对照 `portable-libc-subset`、`user-libc-min`、`user-program-build` 和 `posix-like-process-io-subset` 规格，列出缺少实现、缺少规格边界或不应继续公开的声明。
- [x] 1.4 确认可推迟项并记录非目标，包括完整 POSIX libc、hosted libc、动态链接、共享库、locale、线程、宽字符、完整 `FILE` 流、完整 timezone/calendar API、浮点 formatting、`strtok`、`qsort`、`bsearch`、完整 shell/job control、SMP 和 async I/O。

## 2. 公共头文件与 ABI 边界

- [x] 2.1 整理用户态 public headers，使 portable libc subset 的标准 C 子集、POSIX-like wrapper 和 BigOS-specific helper 边界清晰。
- [x] 2.2 增加或修正 freestanding-safe 头文件声明，覆盖本 change 选定的 `ctype.h`、`time.h`、`assert.h`、无符号转换、formatter/error-text、文件/目录/进程 wrapper 和基础类型/宏。
- [x] 2.3 移除、隐藏或降级未实现/未规格化的 hosted 或完整 POSIX 声明，必要时保留 compatibility export 并记录 bounded 语义。
- [x] 2.4 确认 raw syscall helper 仍仅作为显式 opt-in 的 BigOS-specific low-level helper 或 libc 内部实现细节，不鼓励普通程序绕过 errno-translated wrapper。
- [x] 2.5 复查用户态 errno mirror 和共享 ABI 常量，确认数值来源仍与内核单一错误码定义一致。

## 3. libc 成熟子集实现

- [x] 3.1 实现或稳定 ASCII/C-locale-style `ctype` helper，覆盖常见分类和 `toupper`/`tolower`，不依赖 locale、Unicode、宽字符或宿主 libc。
- [x] 3.2 实现或稳定 bounded `time.h` 支持，只公开由现有 bounded time primitive 支撑的类型、常量和 helper，不声明 timezone、locale、calendar conversion、sleep/timer 或完整 POSIX time API。
- [x] 3.3 实现或稳定 freestanding-safe `assert.h` 支持，覆盖 `NDEBUG` 禁用语义和断言失败的确定性诊断/终止路径，不依赖 hosted libc、动态初始化、线程、信号或完整 hosted `abort`。
- [x] 3.4 实现或稳定无符号数值转换 helper，至少覆盖 `strtoul`，并按需要补齐更宽类型变体；确保 base、end pointer、无数字、溢出和 errno 语义确定。
- [x] 3.5 成熟 shared bounded formatter，使 `printf`、`fprintf(stderr, ...)` 和 `snprintf` 的整数、无符号、指针、`size_t`、long、简单宽度、截断和返回值行为一致。
- [x] 3.6 增加或稳定错误文本 helper，使 `strerror`/`perror` 或等价 bounded helper 对已支持 errno 产生确定性输出，并为未知 errno 提供 documented fallback。
- [x] 3.7 实现或稳定第一批无隐藏状态 search helper：`strchr`、`strrchr`、`strstr` 和 `memchr`；确保不修改输入、不使用隐藏 tokenizer 状态、不越过 NUL 终止或 caller-provided memory range。
- [x] 3.8 确认 `strtok`、`qsort` 和 `bsearch` 不作为本 change 的必选 public support 暴露；若实施中发现直接消费者，记录为后续独立规格或显式扩展任务。
- [x] 3.9 复查文件、目录、cwd、wait、time、identity、pipe/dup 和错误报告 wrapper，确保成功不改写 errno、失败按内核负 errno 翻译并返回 documented sentinel。

## 4. 代表性用户程序消费路径

- [x] 4.1 新增或更新 representative portable-small-program smoke，覆盖 public header inclusion、静态链接、`argc`/`argv`/环境读取、ctype、`time.h`、`assert.h`、数值解析、`strchr`/`strrchr`/`strstr`/`memchr`、formatter、stdout/stderr、errno/error text、文件或目录 wrapper 和失败路径。
- [x] 4.2 迁移适合的 packaged `/bin/*` 工具，使其优先消费 portable libc subset public API，而不是 raw syscall、私有声明或手写重复 helper。
- [x] 4.3 确认新增或更新的用户程序仍通过现有 freestanding `crt0` + user libc 静态 ELF64 `ET_EXEC` 构建路径和 bounded 镜像安装路径打包。
- [x] 4.4 确认用户程序改动不要求宿主 libc、动态 loader、共享库、TLS runtime、新镜像格式、新 storage driver 或完整 POSIX runtime。

## 5. 验证与记录

- [x] 5.1 运行最窄有用构建检查，优先使用当前 `xmake` 与 `x86_64-elf-gcc`/`x86_64-elf-g++` cross toolchain；若工具缺失，记录缺失条件和残余风险。
- [x] 5.2 运行 portable libc subset 相关用户态 smoke；如通过 Python helper 启动 emulator，使用 `uv run python ...`，若 `uv`、QEMU、Bochs、ROM/display、磁盘镜像配置或 timeout oracle 不可用，记录跳过原因和替代检查。
- [x] 5.3 对 C 用户态源文件和 public headers 执行可行的源码级检查；若使用 clang/clangd 辅助诊断，记录其与 freestanding x86_64 cross-build 配置的差异，且不以其替代 xmake cross-toolchain 构建。
- [x] 5.4 复查本 change 未修改 C++ source/header/build 配置；若实施中触及 C++ 或 C++ build 配置，补充 clang/clangd 辅助静态检查任务并修复本 change 引入的有效诊断。
- [x] 5.5 确认验证记录区分已通过检查、环境不可用跳过项、历史诊断、本 change 新引入问题和剩余风险。

## 6. 文档同步

- [x] 6.1 更新 docs/en 中用户态 libc/API 边界说明，描述 portable libc subset、bounded C library subset、POSIX-like wrapper subset 和非目标。
- [x] 6.2 同步更新 docs/zh 对应镜像文档，保持与 docs/en 相同 ABI 事实、能力边界和非目标。
- [x] 6.3 检查 `roadmap.md` 是否只需规划层级状态更新；不得加入文件路径、命令、验证 marker、实现细节或 archive/version 索引。
- [x] 6.4 检查文档、OpenSpec 和头文件注释没有声明完整 POSIX libc、动态链接、共享库、hosted libc、locale、线程、完整 `FILE` 流、完整 timezone/calendar API 或广泛 POSIX 兼容。

## 验证记录

- 已通过：`uv run pytest tests/test_user_c_baseline_source.py`（6 passed）。
- 已通过：`xmake` 默认配置构建。
- 已通过：`xmake f --userland_smoke=y` 后执行 `xmake`，确认 `/bin/smoke/libc_subset` 通过现有 `crt0 + user libc + -nostdlib -static` ELF64 路径构建。
- 已通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/portable-libc-subset-userland.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 40`，串口观察到 `BIGOS_USERLAND_PASSED`。
- 已复位本地 xmake 配置：`xmake f --userland_smoke=n`。
- 未跳过项：本次本地环境具备 `uv`、xmake、x86_64 cross toolchain、QEMU headless、Legacy BIOS raw image 构建与 marker timeout oracle。
- 本 change 未修改 C++ source/header 或 C++ build 配置；未运行 clang/clangd 辅助诊断。
