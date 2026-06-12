## 1. ABI 与契约定义

- [x] 1.1 定义 BigOS 有界元数据公共结构、类型常量、mode/owner 字段约定、用户可见对象编号零值规则和保留字段零填充规则。
- [x] 1.2 分配并记录路径查询与 fd 查询的 syscall 契约，确认不改变现有 `int 0x80` 向量和寄存器约定。
- [x] 1.3 审查 ABI 字段宽度、对齐、用户/内核共享头边界和未来扩展保留位，避免暗示完整 POSIX `struct stat` 兼容。
- [x] 1.4 明确 unsupported 字段、只读 exFAT 默认值、`/rw` 运行期值、对象编号不暴露后端 inode/编号和跨重启非持久化边界。

## 2. 内核 VFS 与后端实现

- [x] 2.1 为 VFS 增加统一 metadata snapshot 填充路径，覆盖绝对路径查询和 open file object/fd 查询。
- [x] 2.2 为只读 exFAT 后端补齐常规文件与目录的最小元数据返回，并确认查询不修改磁盘镜像、MBR、分区或 exFAT 状态。
- [x] 2.3 为 RAM-backed `/rw` 后端补齐元数据返回，使 create、write、truncate、mkdir、unlink 和权限元数据变化后的查询结果可解释。
- [x] 2.4 接入 fd table 查询，保证有效 fd 查询不推进 offset，dup/dup2 后 fd 查询同一 open file object，目录 fd 仅覆盖既有目录 open file object 或最小目录枚举路径，非法 fd 确定性返回错误。
- [x] 2.5 覆盖 unlink 后路径查询与仍打开 fd 查询的区别，确保 open file 引用归零前 fd metadata 仍可用。
- [x] 2.6 审查所有查询路径的阻塞上下文、分配失败、后端失败和不可阻塞上下文行为，禁止 IRQ/调度临界区执行阻塞 I/O。

## 3. Syscall 与用户内存边界

- [x] 3.1 在 syscall dispatcher 中接入元数据路径查询和 fd 查询，复用现有用户指针读取、路径长度限制和 VMA-backed 复制边界。
- [x] 3.2 确保成功前完整初始化内核临时元数据结构，用户缓冲非法、路径失败、fd 失败或后端失败时不复制部分结果。
- [x] 3.3 统一负 errno 映射，覆盖 missing path、bad fd、fault、unsupported path form、permission denied、backend failure 和 no memory。
- [x] 3.4 回归检查 syscall 路径不发送 i8259 EOI、不改变 user entry、CR3 切换、GDT/TSS 或异常处理约定。

## 4. 用户态 libc 与工具消费

- [x] 4.1 在 freestanding libc 中增加 `stat`/`fstat` 风格 wrapper、用户态 errno 翻译和失败哨兵语义。
- [x] 4.2 增加或更新用户态公共头，暴露最小元数据结构、类型/mode 常量和 wrapper 原型，不声明未实现 hosted/POSIX 接口。
- [x] 4.3 增加小型打包 `stat` 风格用户程序，用确定性格式展示对象类型、大小和已支持的 mode/owner 字段。
- [x] 4.4 确保用户工具对缺失路径、非法 fd、不支持路径形式和权限失败输出确定性错误，并让 shell 保持可恢复交互循环。
- [x] 4.5 审查用户程序构建和打包路径，确认不引入动态链接、共享库、完整 libc、glob、完整 `ls -l` 或脚本环境语义。

## 5. 行为验证与文档

- [x] 5.1 增加行为断言覆盖只读 exFAT 文件、`/rw` 常规文件、目录、成功路径查询、成功 fd 查询和用户工具输出。
- [x] 5.2 增加失败行为断言覆盖缺失路径、不支持路径形式、非法 fd、非法用户缓冲、后端失败和 fd offset 不推进。
- [x] 5.3 增加 unlink 后路径查询失败但仍打开 fd 查询有效的运行期行为验证。
- [x] 5.4 更新相关文档或验证说明，明确该能力是 BigOS bounded metadata subset，不是完整 POSIX `stat`、设备节点、符号链接、ACL、xattr 或持久 inode 语义。
- [x] 5.5 记录工具链、QEMU/Bochs、ROM/display、磁盘镜像、串口 oracle 或 timeout 依赖；不可用时明确跳过原因和残余风险。

## 6. 构建与静态检查

- [x] 6.1 运行最窄可用 `xmake` 交叉构建，确认 kernel、user libc 和新增用户程序可由 `x86_64-elf-gcc`/`x86_64-elf-g++` 构建。
- [x] 6.2 对新增或修改的 C++ 源/头执行接近 freestanding C++17/x86_64 交叉环境的 clang 辅助检查，记录历史诊断、当前变更新诊断和不可用工具风险。
- [x] 6.3 对新增或修改的 C++ 源/头执行 clangd 辅助诊断，修复当前变更新引入的有效错误，记录 freestanding 配置差异。
- [x] 6.4 若新增或修改 Python helper/test，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若未涉及 Python，记录不适用。
- [x] 6.5 在环境支持时运行最窄元数据 runtime smoke；若 QEMU、Bochs、交叉工具链或镜像不可用，记录跳过而非声称通过。

## Validation Notes

- `xmake` 通过；链接阶段保留既有 `LOAD segment with RWX permissions` 警告。
- `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -nostdinc++ -Iinclude -Icpp/include -Icpp/libsupc++/include -Ikernel/mm -Ikernel/core -fsyntax-only kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc kernel/core/proc/proc.cc` 通过；首次缺少 `cpp/libsupc++/include` 时出现 `<new>` 头缺失，补齐 freestanding include 后通过。
- `GetDiagnostics` 无当前诊断。
- `uv run pytest tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_user_c_baseline_source.py tests/test_boot_debug.py` 通过，59 passed。
- `uv run ruff check tools/boot_debug.py tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_user_c_baseline_source.py tests/test_boot_debug.py` 通过。
- `uv run ruff format --check tools/boot_debug.py tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_user_c_baseline_source.py tests/test_boot_debug.py` 通过。
- `uv run pyright tools/boot_debug.py tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_user_c_baseline_source.py tests/test_boot_debug.py` 通过；仅提示 pyright 有新版本。
- `xmake f --userland_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/metadata-serial.log --expect-serial-marker BIGOS_USERLAND_PASSED` 通过；QEMU headless 观察到 `BIGOS_USERLAND_PASSED`。boot artifact build 仍有既有 `movsd`/`movsl` assembler warnings。
- 未运行 Bochs 交叉验证；当前最窄 runtime smoke 使用 QEMU headless、构建镜像、串口 oracle 和默认 timeout。
