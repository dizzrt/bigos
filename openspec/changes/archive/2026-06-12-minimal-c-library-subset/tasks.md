## 1. 接口盘点与边界收敛

- [x] 1.1 盘点 `user/libc`、`user/bin`、`user/sh`、`user/init` 和用户态 smoke 对 libc 接口的实际依赖，确认它们属于最小 C 标准库子集。
- [x] 1.2 对照 `user-libc-min` 规格整理 `stdio.h`、`stdlib.h`、`string.h`、`errno.h`、`unistd.h`、`fcntl.h`、`sys/types.h`、`sys/wait.h` 和 umbrella 头暴露的类型、常量、函数原型和 non-goals，移除或记录任何暗示 hosted/POSIX 完整语义的声明。
- [x] 1.3 确认用户态 errno mirror 与内核单一错误码来源数值一致，并记录任何需要同步的 errno 常量。
- [x] 1.4 确认本 change 不改变 boot 地址、链接地址、IDT/syscall vector、页表布局、磁盘布局、用户 ELF 装载 ABI 或现有 `int 0x80` 寄存器约定。

## 2. libc 行为实现与收敛

- [x] 2.1 收敛 syscall wrapper 的成功/失败返回约定，确保负 errno 翻译为用户态正 `errno` 与 `-1`/接口失败哨兵，成功时不改写 `errno`。
- [x] 2.2 补齐或修正字符串/内存函数的有界标准语义，重点覆盖 `memmove` 重叠区间、`strncmp` 长度限制和 NULL 不被额外承诺的边界。
- [x] 2.3 收敛最小堆分配行为，确认 `malloc` 对齐、失败返回 NULL、失败不破坏既有块、`free(NULL)` 无副作用，并记录不承诺线程安全或完整 coalescing。
- [x] 2.4 收敛最小 stdio 行为，确认 `putchar`、`puts`、最小 `printf` 和 `fprintf(stderr, ...)` 只依赖 fd/write，且仅承诺 `%s`、`%d`、`%x`、`%c`、`%%`。
- [x] 2.5 引入最小 `stdin`、`stdout`、`stderr`/opaque `FILE` 表示，仅用于 standard streams，确认不实现 `fopen`、`fclose`、完整 buffering 或 hosted `FILE` 语义。
- [x] 2.6 确认只读环境访问通过 crt0/envp、`environ` 和 `getenv` 工作，且不实现 `setenv`、`putenv`、`unsetenv` 等写入语义。

## 3. 用户程序与行为用例

- [x] 3.1 新增专门的 libc subset smoke，覆盖参数读取、环境读取、stdout 输出、`fprintf(stderr, ...)` 错误报告、errno 翻译和正常退出状态。
- [x] 3.2 增加堆分配行为用例，覆盖成功分配、写入读取、释放复用或释放无副作用，以及分配失败路径可观察行为。
- [x] 3.3 增加 stdio/格式化输出用例，覆盖 `printf`、`fprintf(stderr, ...)`、已支持格式符和不依赖完整 `FILE` 流的输出路径。
- [x] 3.4 确认新增用例仍是静态、freestanding 用户程序，不依赖宿主 libc、动态链接器、共享库、线程 runtime 或完整 POSIX shell 行为。

## 4. 文档与规格同步

- [x] 4.1 更新相关用户态 libc 文档或源内公共头注释，明确 bounded minimal subset 与 non-goals。
- [x] 4.2 如果修改 `docs/en`，同步更新 `docs/zh` 的对应相对路径；如果未修改文档树，在实现记录中说明原因。
- [x] 4.3 确认 `roadmap.md` 保持项目规划层级，不加入实现入口、文件路径、命令、验证 marker 或归档索引。

## 5. 验证与记录

- [x] 5.1 运行最窄可用构建检查，优先使用 `xmake` 验证用户态 libc 与用户程序仍可构建；若缺少 `x86_64-elf-gcc`、`xmake` 或相关工具，记录阻塞原因。
- [x] 5.2 运行 OpenSpec 校验/状态检查，确认 `minimal-c-library-subset` 的 proposal、design、specs 和 tasks 可被 OpenSpec 识别。
- [x] 5.3 在环境具备时运行专门的 libc subset smoke，并保留现有 userland smoke 作为组合路径回归，验证最小 libc 子集的输出、`fprintf(stderr, ...)`、errno、参数/环境和堆行为；如需 Python helper，使用 `uv run python ...`。
- [x] 5.4 若 QEMU、Bochs、交叉工具链、显示/ROM 依赖或磁盘镜像配置不可用，明确记录跳过的运行时验证、已执行替代检查和剩余风险。
- [x] 5.5 复核当前 change 未引入 C++ 源、C++ 头或 C++ 构建配置变更；若实现阶段触及 C++，补充 clang/clangd 辅助诊断任务并区分历史诊断与本次新增问题。
