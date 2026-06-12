## 1. 行为盘点与边界确认

- [x] 1.1 盘点 `kernel/core/proc`、`kernel/core/syscall`、`kernel/core/fs`、pipe/dup、signals、time/identity、`user` 中 libc/init/shell/小型程序对 Stage 23 子集的现有支持情况，列出已满足、需补齐和需明确 non-goal 的行为。
- [x] 1.2 确认本 change 不改变 boot 地址、链接地址、IDT/syscall vector、页表布局、磁盘布局、用户 ELF 装载 ABI、CR3 切换假设或现有 `int 0x80` 寄存器约定。
- [x] 1.3 对照 `posix-like-process-io-subset` 规格复核当前文档和用户可见描述，确保只声明 bounded POSIX-like process/I/O subset，不暗示完整 POSIX。

## 2. 进程与 syscall 语义收敛

- [x] 2.1 收敛进程生命周期、退出状态、wait/reap 和进程表槽位复用行为，确保父进程可观察子进程完成且回收路径不永久占用资源。
- [x] 2.2 收敛 `execve` 镜像替换的成功路径，确认参数、环境、fd 继承和静态用户 ELF 装载行为符合 bounded subset。
- [x] 2.3 收敛 `execve` 失败路径，确保缺失、非法或不支持的目标通过 documented errno/返回值报告，并且调用进程在可恢复失败点保持有效。
- [x] 2.4 复核 syscall wrapper 的错误转换，确保用户态继续通过 `-1`/失败哨兵和正 `errno` 观察失败，不直接依赖内核负 errno。

## 3. fd、pipe 与 shell 组合路径

- [x] 3.1 收敛 fd 0/1/2 的标准输入、输出和错误路径，确认简单 C 程序、PID-1 init 和 `/bin/sh` 的默认 fd 行为可观察。
- [x] 3.2 收敛 fd inheritance、dup/dup2 和 close 行为，确保 duplicate fd 共享同一 bounded underlying object，关闭一个 duplicate 不破坏其他仍打开 fd。
- [x] 3.3 收敛 shell 支持的 redirection 行为，确保只影响目标命令 fd 映射，失败时不破坏 shell 或父路径的无关 fd。
- [x] 3.4 收敛 pipe endpoint 生命周期、FIFO 数据传输、EOF 和错误行为，确认关闭所有写端后读端能观察 documented EOF，且无关 fd 保持可用。
- [x] 3.5 收敛 shell 支持的 pipe command composition，确保上游命令 stdout 能传递给下游命令 stdin，并通过输出、退出状态或确定性日志观察。

## 4. signals、time、identity 与用户态边界

- [x] 4.1 收敛 supported signal delivery 或 termination 行为，确认它保持在当前单核、有界用户进程模型内，不暗示 process group、session 或 terminal control。
- [x] 4.2 收敛 time syscall wrapper 的用户可见返回值，确保简单 C 程序能获得 documented BigOS time primitive。
- [x] 4.3 收敛 identity syscall wrapper 的用户可见返回值，确保返回 stable bounded identity values，且不暗示完整 POSIX users/groups/credentials/permissions。
- [x] 4.4 复核用户态 libc 和 shell 错误报告，确保 unsupported command、redirection、pipe 或语法形式通过可观察错误路径报告。

## 5. 行为用例与文档同步

- [x] 5.1 新增或更新 process/I/O subset 行为用例，覆盖 fork/exec/wait、退出状态、fd inheritance、stdout/stderr 输出和 errno 错误传播。
- [x] 5.2 新增或更新 fd/pipe/redirection 行为用例，覆盖 dup/dup2、close、redirection、pipe 数据传输、pipe EOF 和无关 fd 保持可用。
- [x] 5.3 新增或更新 shell command execution 行为用例，覆盖 packaged program launch、unsupported behavior error、可观察输出和有界语法边界。
- [x] 5.4 如需更新文档，优先更新 `docs/en` 并同步 `docs/zh` 对应相对路径；如未修改文档树，在实现记录中说明原因。
- [x] 5.5 确认 `roadmap.md` 保持项目规划层级，不加入具体入口点、文件路径、命令、验证 marker、源码实现细节或归档索引。

## 6. 验证与记录

- [x] 6.1 运行 OpenSpec 校验/状态检查，确认 `stabilize-posix-like-process-io-subset` 的 proposal、design、specs 和 tasks 可被识别。
- [x] 6.2 运行最窄可用构建检查，优先使用 `xmake` 验证内核、用户态 libc、shell 和小型用户程序仍可构建；若缺少 `x86_64-elf-gcc`、`xmake` 或相关工具，记录阻塞原因。
- [x] 6.3 在环境具备时运行 process/I/O subset runtime smoke 或扩展后的 userland smoke，覆盖进程生命周期、exec/wait、fd inheritance、dup/redirection、pipe、signals、time/identity、shell 命令执行和错误输出；如需 Python helper，使用 `uv run python ...`。
- [x] 6.4 若 QEMU、Bochs、交叉工具链、显示/ROM 依赖或磁盘镜像配置不可用，明确记录跳过的运行时验证、已执行替代检查和剩余风险。
- [x] 6.5 如果实现阶段修改 C++ 源、C++ 头、kernel C++ support、KTL 或 C++ 构建配置，补充 clang/clangd 辅助诊断任务，按 freestanding C++17/x86_64/no exceptions/no RTTI 约束配置，并区分历史诊断、本次新增问题和工具链 false positives。
- [x] 6.6 如实现阶段修改 Python helper，补充并运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录该 blocker 而不是静默使用系统 Python。

## 实现记录

- 盘点结果：现有内核与用户态已具备 bounded `fork`/`execve`/`wait`、fd/VFS、pipe、dup/dup2、signals、time/identity、PID-1 init、`/bin/sh` 和小型用户程序基线；本次补齐用户可观察 wait status、组合行为用例和文档边界措辞。
- 底层约束：未改变 boot 地址、链接地址、IDT/syscall vector、页表布局、磁盘布局、用户 ELF 装载 ABI、CR3 切换假设或 `int 0x80` syscall number/register 约定；`SYS_WAIT` 保持 number=4，仅使用既有第二参数寄存器作为可选 status 指针。
- 行为覆盖：`user/smoke/userland_smoke.c` 现在覆盖 failed `execve` 后 caller 存活、`wait_status` raw exit status、dup 后 close 原 fd、pipe EOF、shell pipe、shell `<`/`>` redirection、unsupported pipe syntax error、signal default termination、time/identity wrapper。
- 文档同步：更新 `docs/en/arch/syscall-entry.md`、`docs/zh/arch/syscall-entry.md`、`docs/en/arch/first-user-program.md`、`docs/zh/arch/first-user-program.md` 的 wait status 表述；更新 `roadmap.md` 和本 change proposal，避免暗示完整 POSIX。
- 验证记录：`openspec status --change "stabilize-posix-like-process-io-subset" --json` 通过；`openspec validate "stabilize-posix-like-process-io-subset" --type change --strict --json` 通过；`uv run pytest tests/test_process_lifecycle_source.py tests/test_user_c_baseline_source.py tests/test_syscall_entry_source.py` 通过，22 passed；`xmake` 通过，保留既有 `LOAD segment with RWX permissions` linker warning。
- 运行时记录：`xmake f --userland_smoke=y` 后运行 `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland_smoke_serial.log --expect-serial-marker BIGOS_USERLAND_PASSED` 通过并观察到 `BIGOS_USERLAND_PASSED`；随后已执行 `xmake f --userland_smoke=n` 恢复默认配置。
- 环境与剩余风险：本次 QEMU、xmake、cross toolchain 和 `uv` 可用，未因环境缺失跳过必要验证；Bochs cross-validation 非本 change 必需项，未运行。
- 诊断记录：对 `kernel/core/syscall/syscall.cc`、`user/libc/syscall.c`、`user/smoke/userland_smoke.c` 运行编辑器 diagnostics，未发现新增诊断；未修改 Python helper，因此 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和全量 `uv run pytest` 不适用。
