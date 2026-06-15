## 1. 接口盘点与边界确认

- [x] 1.1 盘点 `user/libc/include`、umbrella header、raw syscall primitive、BigOS-specific helper 和 bundled user programs 的声明/包含关系，形成 public、compatibility、internal 分类清单。
- [x] 1.2 对照 `user-libc-min`、`user-crt0-runtime`、`posix-like-process-io-subset` 规格，标记缺少实现、缺少规格或缺少文档边界的声明。
- [x] 1.3 确认 Stage 40 第一批必须补齐 `calloc`、`realloc`、`strtol`、`atoi`、bounded `snprintf`、`DIR*` 风格目录 wrapper 和增强 formatter，并记录延后项，例如完整 hosted stdio、动态链接、完整 POSIX libc、locale、threads、完整 precision/flags 和 broad file-backed `mmap`。

## 2. 公共头文件与 ABI 收敛

- [x] 2.1 整理用户态 public headers，使标准 C 子集、POSIX-like bounded wrapper、BigOS-specific public helper 和 internal-only helper 的暴露边界清晰。
- [x] 2.2 确保新增或保留的 public declaration 都有对应实现、规格覆盖和文档边界；未实现接口不得继续通过 public headers 暗示支持。
- [x] 2.3 保持 userland errno mirror 与内核统一 errno 来源一致，确认成功 wrapper 不改写 `errno`、失败 wrapper 设置正 errno 并返回接口失败哨兵。
- [x] 2.4 复查 raw `syscall0`-`syscall6` 的暴露方式，确保它们仅作为 BigOS-specific documented low-level helper 或 libc 内部实现细节保留，不从普通 umbrella header 暴露给普通用户程序。

## 3. libc 基础子集实现

- [x] 3.1 补齐或稳定 Stage 40 第一批基础 stdlib/string/memory 函数，至少覆盖 `strtol` 和 `atoi`，确保 freestanding-safe、有界输入确定性和失败不破坏调用者状态。
- [x] 3.2 补齐或稳定 allocator helper，至少覆盖 `calloc` 和 `realloc`，保持溢出检查、有界失败语义、原块失败保留和 `free(NULL)` 无副作用。
- [x] 3.3 增强 bounded stdio/error reporting，覆盖 shared formatter、bounded `snprintf`、宽度、`%u`、`%p`、`%ld`、`%lu`、`%zu`，不得引入完整 hosted `FILE` 流、浮点、locale、宽字符或完整 precision/flags。
- [x] 3.4 整理文件、进程、cwd、time、signal、wait、pipe、dup 等 wrapper，使简单静态 C 程序能通过 libc 消费现有 bounded syscall 行为。
- [x] 3.5 引入接近 POSIX 的 `DIR*` 风格目录枚举 wrapper，并明确其为 BigOS bounded subset，不声明完整 POSIX `opendir`/`readdir`/`closedir` 语义。
- [x] 3.6 保持 `crt0` 静态入口契约不变，确认 `argc`、`argv`、`envp`、`environ` 和 `SYS_EXIT` 交接不依赖动态 loader、共享库或 hosted runtime。

## 4. 用户程序消费路径

- [x] 4.1 更新 packaged `/bin/*` 工具中适合迁移的代码，使其优先消费 Stage 40 public libc API，而不是不必要地依赖 raw syscall 或隐式内部声明。
- [x] 4.2 更新 shell、PID-1 init 或 smoke 程序的 libc 使用方式，确保行为仍属于 bounded POSIX-like process/I/O subset。
- [x] 4.3 为 BigOS-specific helper 保留必要兼容路径，并记录哪些 helper 是公开支持、compatibility export 或后续清理候选。

## 5. 验证

- [x] 5.1 增加或更新 libc subset smoke，覆盖参数/环境读取、errno 翻译、成功调用不改写 errno、stdout/stderr 输出、`snprintf`/formatter、`strtol`/`atoi`、`calloc`/`realloc`、`DIR*` wrapper 和 wrapper 失败路径。
- [x] 5.2 运行最窄有用构建检查，优先使用当前 xmake 与 x86_64 cross toolchain；若 `x86_64-elf-gcc`、`xmake` 或相关工具不可用，记录缺失条件和残余风险。
- [x] 5.3 在环境可用时运行用户态相关 QEMU headless smoke；如需 Python helper，使用 `uv run python ...`，若 `uv`、QEMU、Bochs、ROM/display 或磁盘镜像配置不可用，明确记录跳过原因。
- [x] 5.4 对任何 C/C++ 头文件或源文件变更执行可行的辅助静态检查；若 clang/clangd 与 freestanding cross-build 配置不匹配，记录工具差异和剩余风险。
- [x] 5.5 确认验证记录区分已通过检查、环境不可用跳过项、历史诊断和本 change 新引入问题。

## 6. 文档同步

- [x] 6.1 更新 docs/en 中的用户态 libc/API 边界说明，描述 Stage 40 bounded C library subset 和明确非目标。
- [x] 6.2 同步更新 docs/zh 对应镜像文档，保持与 docs/en 相同 ABI 事实和兼容边界。
- [x] 6.3 检查 `roadmap.md` 如需微调，仅保持项目规划层级，不加入文件路径、命令、验证 marker、实现细节或 archive/version 索引。
- [x] 6.4 确认所有文档使用 bounded subset / POSIX-like subset 表述，不声明完整 POSIX libc、动态链接、共享库或广泛 POSIX 兼容。

## 实施与验证记录

- 分类结果：标准 C 子集包含 `stdio.h`、`stdlib.h`、`string.h`、`errno.h`；POSIX-like bounded wrapper 包含 `unistd.h`、`fcntl.h`、`sys/types.h`、`sys/wait.h`、`sys/stat.h`；BigOS-specific public helper 保留在 `bigos_dirent.h` 与既有 wrapper 中；raw syscall primitive 移至显式 opt-in 的 `bigos_syscall.h`，不再由 `libc.h` umbrella 暴露。
- 实现结果：补齐 `calloc`、`realloc`、`strtol`、`atoi`、`snprintf`、共享 bounded formatter、常用宽度、`%u`、`%p`、`%ld`、`%lu`、`%zu`，并新增有界 `DIR*` wrapper。
- 用户程序结果：`/bin/ls` 迁移到 `opendir`、`readdir`、`closedir` 与 bounded stdio；`libc_subset` smoke 覆盖新增 stdlib、allocator、formatter、DIR wrapper、errno 和失败路径。
- 文档结果：`docs/en/arch/userland-runtime.md`、`docs/zh/arch/userland-runtime.md`、`docs/en/arch/runtime-smoke-validation.md`、`docs/zh/arch/runtime-smoke-validation.md` 已同步；`roadmap.md` 保持规划层级，无需加入实现细节。
- 已通过检查：`xmake`；`xmake f --userland_smoke=y`；`xmake`；`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-smoke-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`；`openspec status --change "standard-c-library-foundation" --json`；`openspec validate standard-c-library-foundation --strict`。
- 历史诊断：QEMU smoke 中 boot artifact 汇编仍报告既有 `movsd`/`movsl` warning；本 change 未新增该 warning，且 runtime marker 已观察到 `BIGOS_USERLAND_PASSED`。
