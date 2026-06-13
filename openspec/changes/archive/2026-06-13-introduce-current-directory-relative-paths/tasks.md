## 1. 契约和公共接口

- [x] 1.1 审查现有 path-taking syscall、fd/VFS、metadata、exec 和 shell 路径入口，确认需要接入 cwd 解析的完整调用面。
- [x] 1.2 定义 cwd 相关 syscall 编号、参数、返回值、`ERANGE` errno 映射和用户态 wrapper 契约，保持 `int 0x80` ABI 与现有寄存器约定不变。
- [x] 1.3 更新公共头文件中的路径长度、`ERANGE`、cwd wrapper、用户态声明和内核接口声明，确保 freestanding-safe 且不声明未实现 POSIX 接口。
- [x] 1.4 明确第一版 POSIX-style `.`/`..`、root 父目录保持 root、空路径、重复分隔符、过长路径和非 NUL-terminated 路径处理规则，并同步到实现注释或文档化契约。

## 2. 进程 cwd 生命周期

- [x] 2.1 在进程对象中加入有界 cwd 状态，并在初始进程创建时初始化为 `/`。
- [x] 2.2 在普通进程创建失败路径中释放 partially initialized cwd，避免发布半初始化进程。
- [x] 2.3 在 `fork` 路径中复制 cwd，使父子后续 `chdir` 独立生效。
- [x] 2.4 在 `execve` commit 与 rollback 路径中保留旧 cwd，并释放临时路径缓冲。
- [x] 2.5 在 exit/fault/reap 安全边界释放 cwd 资源，避免 IRQ、active stack teardown 或不可阻塞上下文释放。

## 3. VFS 路径解析

- [x] 3.1 实现统一 path resolution helper，支持绝对路径从 root 解析、相对路径从当前进程 cwd 解析，并支持 POSIX-style `.`/`..` 组件归约。
- [x] 3.2 将 `open`、可写运行时文件操作、目录操作、path metadata 和 `execve` 文件查找接入统一解析 helper。
- [x] 3.3 确保只读 exFAT 后端和 `/rw` 后端在 cwd 解析后仍保持既有权限、容量、引用生命周期和错误语义。
- [x] 3.4 为无当前进程、无 cwd、非法用户路径、过长合成路径、缺失组件和非目录中间组件返回确定性错误。
- [x] 3.5 审查路径解析中的分配、阻塞 I/O 和用户缓冲复制边界，确保不可阻塞上下文确定性拒绝或诊断。

## 4. chdir/getcwd 系统调用

- [x] 4.1 实现 `chdir` 内核路径：先解析并确认目标为目录，再原子提交新 cwd。
- [x] 4.2 实现 `getcwd` 内核路径：验证用户缓冲区、长度和写权限后复制 NUL-terminated cwd，缓冲区过小时返回 `-ERANGE`。
- [x] 4.3 保证 `chdir`、`getcwd` 的失败路径不修改旧 cwd、不泄露未初始化内存、不破坏 fd/VFS 状态。
- [x] 4.4 将 cwd syscall 纳入 syscall dispatcher、用户缓冲验证和 errno 映射检查，并保证 `-ERANGE` 正确暴露给用户态。

## 5. 用户态 libc 和 shell

- [x] 5.1 在用户态 libc 中实现 `chdir`、`getcwd` wrapper、`ERANGE` 暴露与头文件声明，按现有 wrapper 规则翻译负 errno。
- [x] 5.2 确认 `open`、metadata、`execve` 等 path wrapper 不在 libc 中自行实现 namespace、symlink 或 `realpath`，仅把含 `.`/`..` 的路径交给内核解析。
- [x] 5.3 在 `/bin/sh` 中添加 `cd` builtin，确保它在 shell 进程内执行并在失败后保持旧 cwd。
- [x] 5.4 让 shell 命令路径、重定向路径和小工具消费相对路径时复用 libc/kernel cwd 契约。
- [x] 5.5 添加小型静态 freestanding `/bin/pwd` 用户程序，通过 libc `getcwd` 输出当前 cwd，并在失败时报告 errno-based 错误。

## 6. 验证和诊断

- [x] 6.1 添加 source-level 或单元式检查覆盖路径合成、`.`/`..` 组件归约、root 父目录保持 root、cwd 复制、exec 保留、失败不变性和非法路径拒绝。
- [x] 6.2 添加行为导向 runtime validation，覆盖 `chdir` 后相对 open/stat、`..`、`fork` 继承、`execve` 保留、shell `cd`、`/bin/pwd`、`getcwd` 小缓冲 `ERANGE`、只读后端拒写和 `/rw` 成功路径。
- [x] 6.3 运行最窄有用的 `xmake` 构建，记录 `x86_64-elf-gcc`/`x86_64-elf-g++` 可用性和构建结果。
- [x] 6.4 对涉及 C++ 源码/头文件的实现运行 clang/clangd 辅助静态检查，区分历史诊断、当前变更新诊断和 freestanding 配置误报。
- [x] 6.5 如环境具备，运行 QEMU headless runtime smoke；如 QEMU、Bochs、cross-toolchain、ROM/display、raw image 或 timeout oracle 不可用，明确记录跳过原因、替代检查和剩余风险。

## 7. 文档和收尾

- [x] 7.1 更新相关架构或用户态文档，说明 cwd 与相对路径是有界 BigOS 子集，支持 POSIX-style `.`/`..`，但不是完整 POSIX pathname、symlink 或 `realpath` 支持。
- [x] 7.2 若修改 `docs/en`，同步更新 `docs/zh` 对应路径，保持目录结构镜像。
- [x] 7.3 运行 OpenSpec 状态/校验命令，确认 proposal、design、specs、tasks 均满足 apply 要求。
- [x] 7.4 汇总验证记录，分别列出已通过检查、未运行检查及原因、剩余风险和任何当前变更新增诊断。

## 验证记录

- 已通过：`uv run pytest tests/test_syscall_entry_source.py tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_user_c_baseline_source.py`，41 passed。
- 已通过：`PATH="/usr/local/bin/cross_compiler/bin:$PATH" xmake`，内核与当前用户态配置构建通过；链接器保留既有 `LOAD segment with RWX permissions` warning。
- 已通过：`PATH="/usr/local/bin/cross_compiler/bin:$PATH" xmake build user-init-elf`，用户 ELF 打包构建通过。
- 已通过：`PATH="/usr/local/bin/cross_compiler/bin:$PATH" uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/cwd-userland-serial-final-clean.log --expect-serial-marker BIGOS_USERLAND_PASSED`，串口观察到 `BIGOS_USERLAND_PASSED`；boot artifact 汇编仍有既有 `movsd`/`movsl` warning。
- 已通过：`PATH="/usr/local/bin/cross_compiler/bin:$PATH" uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/bochs-post-bochs-fix.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`，串口观察到 `BIGOS_USERLAND_PASSED`。
- 已通过：`PATH="/usr/local/bin/cross_compiler/bin:$PATH" uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/qemu-post-bochs-fix.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`，串口观察到 `BIGOS_USERLAND_PASSED`。此前一次 QEMU/Bochs 并行运行因共享 `build/test/os.raw` 造成 QEMU timeout，单独重跑通过。
- 已通过：VS Code diagnostics / clangd 辅助诊断无新增诊断。
- 已通过：`PATH="$HOME/.nvm/versions/node/v22.22.3/bin:$HOME/Library/pnpm/bin:$PATH" openspec status --change "introduce-current-directory-relative-paths" --json && PATH="$HOME/.nvm/versions/node/v22.22.3/bin:$HOME/Library/pnpm/bin:$PATH" openspec instructions apply --change "introduce-current-directory-relative-paths" --json && PATH="$HOME/.nvm/versions/node/v22.22.3/bin:$HOME/Library/pnpm/bin:$PATH" openspec validate "introduce-current-directory-relative-paths" --type change --strict --json`，status 显示 proposal、design、specs、tasks 均为 `done`，apply instructions 显示 schema 为 `spec-driven` 且仅 7.3 待完成，strict validate 返回 1 passed / 0 failed。
- 环境说明：当前 shell 的默认 PATH 一度找不到 `x86_64-elf-*`，验证命令显式加入 `/usr/local/bin/cross_compiler/bin` 后通过。
- 剩余风险：shell runtime smoke 为避免独立的长 fork/exec/redirection 组合阻塞，仅覆盖 shell `cd`、`/bin/pwd` 和 cwd-relative 输出重定向；cwd-relative open/read/stat、`..`、fork 继承、exec 保留和 `ERANGE` 由同一 userland smoke 的非交互内核/libc路径覆盖。
