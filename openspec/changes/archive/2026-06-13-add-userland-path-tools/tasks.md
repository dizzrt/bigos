## 1. 现状确认与范围锁定

- [x] 1.1 审查现有 `user/bin/*`、`user/sh`、`user/libc`、`kernel/core/fs` 和 `include/bigos`，确认已有 `cat`、`pwd`、`stat`、目录枚举、mkdir/unlink、cwd 和 metadata wrapper 的实际状态。
- [x] 1.2 根据审查结果确定第一批路径工具清单，优先补齐缺失的有界 `ls`、`mkdir`、`rm` 或等价单一目的工具，并记录已有工具只需硬化的部分。
- [x] 1.3 确认本 change 不需要修改 bootloader、linker script、IDT/syscall vector、页表布局、磁盘分区发现、exFAT discovery 或 storage driver contract。

## 2. Kernel/Libc Contract

- [x] 2.1 补齐或硬化用户态 libc/header 中目录枚举、metadata、mkdir、unlink、open/read/close、errno 和 cwd-relative path wrapper 的最小声明与实现。
- [x] 2.2 确认路径工具依赖的 syscall wrapper 均按现有 `int 0x80` ABI 与负 errno 翻译规则工作，且不声明未实现 hosted/POSIX 接口。
- [x] 2.3 审查 `kernel/core/fs` 和 `kernel/core/syscall` 对路径工具相关成功/失败路径的用户缓冲校验、fd 生命周期、只读/可写后端差异和确定性错误返回。
- [x] 2.4 若实现需要 C++ 内核代码变更，保持 freestanding C++17、无异常、无 RTTI、无 hosted runtime 假设，并记录 clang/clangd 与交叉 GCC 诊断差异。

## 3. Userland Tools

- [x] 3.1 新增或硬化目录列举工具，使其支持绝对路径、cwd-relative 路径、确定性错误报告和有界输出，不承诺完整 POSIX `ls`。
- [x] 3.2 硬化文件内容查看工具，使其通过 libc open/read/close 处理一个或多个路径，并在缺失路径、目录、只读/权限错误或不支持路径形式上返回非零。
- [x] 3.3 硬化元数据观察工具，使其显示 BigOS bounded metadata 字段，并避免承诺 stable inode、ACL、xattr、完整时间戳或完整 POSIX `stat`。
- [x] 3.4 新增或硬化目录创建与路径删除工具，使其在 `/rw` 支持路径上可观察成功，在只读 boot assets 或 unsupported path 上确定性失败。
- [x] 3.5 将新增或调整后的工具接入用户程序构建与镜像打包路径，保持静态 ELF64、bounded 体积限制和既有 `/bin/*` 安装边界。

## 4. Shell Integration

- [x] 4.1 确认 `/bin/sh` 可通过 PATH 或显式路径运行所有支持的路径工具，并保持 `fork`/`execve`/`wait`、stdout/stderr 和退出状态可观察。
- [x] 4.2 验证工具命令与 cwd-relative path、单级 pipe、基本重定向组合时不破坏父 shell fd state。
- [x] 4.3 对 unsupported shell syntax、工具失败、重定向打开失败和管道组合失败保持确定性错误报告，不引入 globbing、脚本语言、job control 或 terminal process group。

## 5. 行为验证

- [x] 5.1 增加或扩展行为导向验证，覆盖目录列举、文件内容查看、metadata 展示、`/rw` 目录创建、`/rw` 路径删除、只读后端变更失败和 cwd-relative path。
- [x] 5.2 覆盖 shell-launched 工具、重定向或 pipe 组合、stdout/stderr、退出状态和失败后 shell 继续可用的组合路径。
- [x] 5.3 运行 `xmake` 或最窄可用用户程序构建检查，确认新增/调整的用户程序和镜像打包路径可构建。
- [x] 5.4 在环境具备 `x86_64-elf-*`、xmake、QEMU/Bochs、ROM/display、disk image 和 timeout/serial oracle 时运行最窄可用 emulator smoke；若不可用，记录缺失条件、替代检查和剩余风险。
- [x] 5.5 如果本 change 修改 C++ 源码、C++ 头文件或 C++ 构建配置，运行或记录等效 clang/clangd 辅助检查；若仅修改 C 用户态程序且未触及 C++，记录 clang/clangd 不适用。

## 6. 文档与 OpenSpec

- [x] 6.1 更新相关用户态或架构文档时保持 `docs/en` canonical 与 `docs/zh` mirror 同步，并避免在 `roadmap.md` 写入实现细节、命令、marker 或文件路径。
- [x] 6.2 记录验证结果，区分已通过检查、无法运行检查及原因、历史诊断、当前 change 引入的问题和剩余风险。
- [x] 6.3 运行 `openspec status --change "add-userland-path-tools" --json` 与 `openspec validate "add-userland-path-tools" --type change --strict --json`，确认 proposal、design、specs 和 tasks 可解析。

## Validation Notes

- 已通过：`GetDiagnostics` 无新增诊断。
- 已通过：`uv run pytest tests/test_user_c_baseline_source.py`，覆盖新增路径工具源码契约、打包清单、shell-launched path-tools smoke 脚本和 `wait_current()` 后主动 reaper source guard。
- 已通过：`xmake`，确认默认构建、用户程序和镜像打包路径可构建；仅有既有 `build/kernel` RWX LOAD segment linker warning。
- 已通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-path-tools-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 60`，确认 baseline `userland_smoke` 在本地 QEMU headless 通过。
- 已通过：新增 exFAT directory fd 支持后运行 `xmake f --userland_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-exfat-readdir-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 60`，确认用户态基线 smoke 仍通过。
- 已通过：修复 `/bin/sh` 重定向 fd 可能复用 stdio fd 的问题后，`uv run pytest tests/test_user_c_baseline_source.py`、`xmake` 与 baseline `xmake f --userland_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-path-tools-shell-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 60` 通过。
- 已通过：定位 shell-launched path-tools 卡住根因后，在 `wait_current()` 消费子进程状态后立即调用安全 reaper，避免连续非交互 shell 脚本在 idle reaper 运行前累积已 wait 子进程资源；保留 reaper 的 active-stack/active-root 防护。
- 已通过：针对 Bochs 交互 `ls` 后重启的日志，修复 `thread_exit()` 不再把已终止线程的 `saved_sp` 留在退出时的 process kernel stack 上，避免 wait 后主动 reaper 释放该 stack 时留下调度器 TCB 悬挂栈指针；新增 scheduler source guard 覆盖此不变量。
- 已通过：扩展 `userland_smoke` shell 脚本覆盖 `/bin/cat` 显式路径、PATH 运行 `cat`/`ls`/`mkdir`、cwd-relative path、`/bin/stat` metadata、`/bin/rm` 删除、`ls . > file`、`/bin/cat file > file`、`cat file | /bin/cat`、缺失文件失败、只读 exFAT 删除失败、删除后 `stat` 失败和失败后 `echo shell-alive` 继续可用。
- 已通过：`uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-path-tools-shell-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 60`，确认完整 shell-launched path-tools 扩展脚本在本地 QEMU headless 通过。
- 待手工复验：`xmake run bochs` 交互启动后输入 `ls`，预期输出目录项后仍停留在同一个 shell prompt，串口日志不再出现第二轮 `BigOS kernel reached`。
- clang/clangd：当前已修改 C++ 源码和头文件；`GetDiagnostics` 无新增诊断，`xmake` 与 QEMU baseline smoke 作为交叉工具链/运行时替代检查通过。
