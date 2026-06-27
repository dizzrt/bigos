## Why

BigOS 当前的用户 ELF 装载路径是 in-kernel、仅接受静态 `ET_EXEC`，并显式拒绝 `PT_INTERP`/`PT_DYNAMIC`/`ET_DYN`；所有 `/bin/*` 程序都把 crt0 与有界 libc 静态链接进各自镜像，导致代码重复、镜像偏大，也无法支持任何跨模块符号共享。`user-runtime-vm-layout` 已为未来动态链接显式预留布局/元数据扩展点，但仍把动态特性标记为运行时拒绝。

本变更面向 Task M12.1，在有界 freestanding-safe 语义内打通"动态链接 + 共享库"机制：让内核接受有界的 `PT_INTERP` + `ET_DYN` 用户镜像并加载用户态解释器（ld.so），由用户态 ld.so 完成有界重定位与符号绑定，并交付一个示例共享库 + 动态可执行程序作为端到端验证，证明跨模块符号解析可用。它明确不声称完整 POSIX 动态链接器、完整 glibc 风格 ABI 或通用 `dlopen` 运行时。

## What Changes

- 扩展内核用户 ELF 装载器（`kernel/core/proc`）：在默认关闭的动态链接路径下，接受有界 `ET_DYN` 主可执行镜像 + 单个 `PT_INTERP` 指定的用户态解释器，按各自基址加载二者的 `PT_LOAD` 段（主镜像与解释器分别加载到布局内的确定性基址），并把控制权交给解释器入口；静态 `ET_EXEC` 路径与既有 bounded 限制保持不变。
- 新增内核初始栈 auxiliary vector（auxv）握手：在既有 `argc`/`argv`/`envp` 初始栈布局之上追加有界 auxv（至少 `AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL` 子集），使用户态 ld.so 能定位主镜像 program header、自身基址与真实入口。
- 新增有界用户态动态链接器 ld.so（`user/` 下，freestanding-safe、自重定位、`-fPIC`）：解析 `PT_DYNAMIC`，加载 `DT_NEEDED` 共享库，执行有界 `R_X86_64_RELATIVE`/`R_X86_64_GLOB_DAT`/`R_X86_64_JMP_SLOT`（及必要的 `R_X86_64_64`）重定位子集与符号绑定，随后跳转到主程序入口。
- 扩展用户构建管线（`xmake/user_package.lua` 等）：新增以 `-fPIC -shared` 构建示例共享库（如 `libdemo.so`）、以 `-fPIC -pie` + 动态段构建一个动态可执行示例程序，并构建/打包 ld.so；构建在默认关闭开关下进行，不改变默认 `/bin/*` 静态程序集合。
- 落实 `user-runtime-vm-layout` 既有预留：把"未来动态链接保留 gap"在有界范围内转为运行时使用的解释器/共享对象映射区，所有新映射仍限制在受支持的用户低半区且不与栈/堆/匿名/file-backed 映射区冲突。
- 新增默认关闭的动态链接验证 smoke：覆盖动态程序加载、ld.so 自重定位、跨模块符号解析调用与确定性失败路径；默认启动仍走静态 `/bin/*`，不依赖任何动态链接组件。
- 不引入：`dlopen`/`dlsym`/`dlclose` 运行时 API、`LD_PRELOAD`/`LD_LIBRARY_PATH` 全量搜索语义、符号版本（versioned symbols）、`IFUNC`/`TLS`（`PT_TLS`/`R_X86_64_*TPOFF*`/`DTPMOD`）、lazy PLT 解析的 `_dl_runtime_resolve` 全量机制（首期可采用 eager/`BIND_NOW`）、多解释器、完整 GNU hash/符号版本表、C++ 全局构造/异常跨模块语义，以及把默认 libc 拆成 `libc.so`。

## Capabilities

### New Capabilities

- `bounded-dynamic-linking`: 定义 BigOS 有界动态链接与共享库能力，覆盖内核对 `ET_DYN` + `PT_INTERP` 镜像的有界加载与解释器移交、初始栈 auxv 握手契约、用户态 ld.so 的有界重定位/符号绑定语义与共享库加载边界、示例共享库 + 动态可执行程序的端到端契约，以及默认关闭验证边界与非目标。

### Modified Capabilities

- `user-elf-program-loader`: 既有需求显式拒绝 `PT_INTERP`/动态链接/`ET_DYN`；修改为在有界动态链接路径下接受单个 `PT_INTERP` + `ET_DYN` 主镜像并加载用户态解释器，同时保持静态 `ET_EXEC` 路径与所有既有 bounded/越界/权限校验不变。
- `user-runtime-vm-layout`: 既有"future dynamic-linking preparation remains non-runtime"需求把动态特性标记为运行时拒绝；修改为在有界范围内启用解释器/共享对象映射区与 auxv 元数据，使原预留 gap 成为受边界约束的运行时布局，且仍拒绝超出有界子集的动态特性。

## Impact

- 影响内核进程/装载层（`kernel/core/proc/proc.cc` 的 ELF 校验/段映射、`copy_exec_args_to_stack` 初始栈构造、`execve`/`launch_init` 提交路径）、公开内核头（`include/bigos/proc.h` 的用户 VM 布局常量与有界限制、`include/bigos/elf.h` 既有动态/重定位结构复用）、用户运行时（`user/` 新增 ld.so 与示例共享库 + 动态可执行程序、crt0 沿用不变）、用户构建管线（`xmake/user_package.lua` 及相关 lua/链接脚本）、默认关闭 build switch 与 smoke 入口。
- 复用既有 fd/VFS 读路径与（M1.1）file-backed 读映射、受限匿名映射与缺页恢复路径供 ld.so 加载共享库；如需在用户选定基址映射只读/可写段，须在有界语义内复用或最小扩展既有映射 syscall，不引入广泛 file-backed `mmap` 或通用 `MAP_FIXED` 语义。
- 不改变启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或既有静态 `ET_EXEC` 装载 ABI；不改变既有 syscall number 取值或语义；默认启动在动态链接组件缺失时仍正常进入 shell 并运行静态 `/bin/*`。
- 初始栈布局在 `envp` NULL 终止符之后追加 auxv，属追加式扩展：既有静态 crt0 仅读取 `argc`/`argv`/`envp`，对其透明；该布局变更需在内核与（动态）用户运行时双侧保持一致，并由 smoke 校验。
- 架构假设为当前 x86_64 freestanding 内核（UEFI 默认启动 backend、Legacy BIOS 交叉验证 backend、单核基线即可）；内存仅经显式内核分配路径或有界静态/栈缓冲获取；工具链以 xmake + x86_64-elf GCC/binutils 为准（依赖 `-fPIC`/`-shared`/`-pie` 与动态段生成能力），辅助 Python 验证通过 `uv run ...`。
- 受当前边界约束：首期采用 eager（`BIND_NOW` 等价）重定位而非 lazy PLT 解析、单一解释器、有界 `DT_NEEDED` 数量与共享对象数量、有界重定位条目数，均作为确定性上限在 spec 中显式声明。
