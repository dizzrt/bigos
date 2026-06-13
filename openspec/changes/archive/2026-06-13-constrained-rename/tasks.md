## 1. 语义确认与接口边界

- [x] 1.1 审查现有 `/rw`、VFS、syscall、libc 和用户态工具实现，确认受限 rename 只覆盖同一 `/rw` 后端内的常规文件路径。
- [x] 1.2 固定第一版错误策略：源路径和目标路径解析为同一父目录同一名称时返回成功 no-op；目标已存在且不是同一目录项时返回 `-EEXIST` 或等价目标已存在错误。
- [x] 1.3 确认本 change 不修改 bootloader、链接地址、IDT/syscall vector、页表布局、CR3 切换、MBR/partition/exFAT 只读发现和 ATA PIO 契约。

## 2. 可写后端与 VFS

- [x] 2.1 在 `/rw` 可写后端实现受限常规文件 rename，先完成权限、容量、类型、父目录和目标状态检查，再提交目录项更新。
- [x] 2.2 保证 rename 失败路径不发布半成品目录项、不移除源项、不破坏目标对象、不提前释放仍被 open fd 引用的 inode/data blocks。
- [x] 2.3 在 fd/VFS 增加路径型 rename 入口，复用 cwd/相对路径解析、后端分派、阻塞上下文守卫和 VFS/backend 错误映射。
- [x] 2.4 覆盖已打开文件、dup 后 fd、独立 open fd 和进程退出/reap 场景下的 rename 引用生命周期。

## 3. Syscall 与 ABI

- [x] 3.1 以 append-only 方式新增 `SYS_RENAME` 或等价 syscall 号，保持既有 syscall 编号、寄存器 ABI、`VECTOR_SYSCALL = 0x80`、DPL 和 no-EOI 语义不变。
- [x] 3.2 在 syscall 层验证并复制 `oldpath` 与 `newpath` 两个有界 NUL 终止用户字符串，非法用户地址、未终止路径、过长路径和溢出范围返回确定性错误。
- [x] 3.3 在进入可能分配、等待或同步块 IO 的 rename 路径前检查调度阻塞守卫，不可阻塞上下文必须拒绝或进入文档化诊断路径。
- [x] 3.4 将 VFS/backend rename 结果稳定映射为用户态可观察的负 errno，避免 panic、IRQ EOI 或 fd table 破坏。

## 4. Libc、头文件与用户态工具

- [x] 4.1 在 freestanding 用户态 libc 中实现 `rename(const char *oldpath, const char *newpath)` wrapper，成功返回 `0`，失败设置 `errno` 并返回 `-1`。
- [x] 4.2 在用户态文件相关头中声明受限 rename wrapper 和必要 errno 常量，不声明未实现的 `renameat`、`renameat2`、link、symlink 或完整目录 rename 接口。
- [x] 4.3 增加小型用户态 rename 工具或等价 shell-consumable 用户程序，支持绝对路径和 cwd-relative 路径，并以 errno-based 错误报告失败。
- [x] 4.4 将 rename 工具纳入现有用户程序构建和打包路径，确保默认 shell 可通过现有命令查找和执行路径观察该能力。

## 5. 验证与文档记录

- [x] 5.1 为后端/VFS/syscall/libc/user 工具补充窄范围源码级或行为断言，覆盖成功 rename、同父目录同名称 no-op、源消失、目标内容保持、目标已存在失败、只读后端失败、缺失源失败和跨后端失败。
- [x] 5.2 运行或记录无法运行的 `openspec validate constrained-rename --strict`，修复当前 change 引入的 OpenSpec 格式问题。
- [x] 5.3 运行或记录无法运行的窄范围 `xmake` 交叉构建；若缺少 `x86_64-elf-*`、xmake、镜像路径或本地配置，明确记录跳过原因和残余风险。
- [x] 5.4 对修改的 C++ 源码/头文件执行或记录无法执行的 clang 与 clangd 辅助静态检查，使用尽量贴近 freestanding C++17、x86_64 target、无异常、无 RTTI 的配置，并区分历史诊断与本 change 新增诊断。
- [x] 5.5 运行或记录无法运行的 QEMU headless rename/userland smoke，验证 shell/user 工具可观察成功和失败路径；如使用 Python helper，命令必须通过 `uv run ...`。
- [x] 5.6 记录验证结果，分开列出已通过检查、因工具链/模拟器/ROM/display/磁盘镜像/serial oracle 不可用而跳过的检查、替代检查、历史问题和当前 change 引入的问题。

## 验证记录

- 已通过：`openspec validate constrained-rename --strict`。
- 已通过：`uv run pytest tests/test_writable_fs_page_cache_pipe_source.py tests/test_fd_vfs_shell_source.py`，共 23 项通过。
- 已通过：`xmake`，完成默认交叉构建；保留既有 linker `LOAD segment with RWX permissions` 警告。
- 已通过：`clang++ -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-pic -fno-pie -mno-red-zone -target x86_64-unknown-elf -Iinclude -Icpp -Ikernel -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc`。
- 已通过：`xmake f --userland_smoke=y` 后执行 `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_USERLAND_PASSED`，观察到 `BIGOS_USERLAND_PASSED`。
- 已恢复：验证后执行 `xmake f --userland_smoke=n`，避免保留默认关闭 smoke 的本地配置。
- 跳过：未单独运行 Bochs 交叉验证；本 change 未修改 boot、BIOS、ATA PIO、IDT 或早期架构路径，QEMU headless 已覆盖用户态可观察 rename 闭环。
- 当前 change 新增问题：无已知；历史/既有问题为默认链接阶段 RWX LOAD segment 警告，以及 boot artifact 汇编中 `movsd`/`movsl` 提示。
