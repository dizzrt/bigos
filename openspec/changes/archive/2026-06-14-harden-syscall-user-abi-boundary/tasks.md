## 1. ABI 盘点

- [x] 1.1 盘点 syscall number、寄存器约定、返回值、负 errno、dispatcher 表、用户 wrapper 和文档中的重复 ABI 事实。
- [x] 1.2 盘点用户态公共头文件，标记已实现且有规格约束的声明、未实现声明、hosted/POSIX 暗示声明和 kernel-private 依赖。
- [x] 1.3 对 `strchr`、`wait_status`、`bigos_readdir`、`brk_raw`、`mmap_anon`、`time_now`、`get_tick` 和 raw `syscall0`-`syscall6` 做逐项归类：public bounded ABI、libc-internal-only、compatibility umbrella export 或 removal candidate。
- [x] 1.4 盘点 crt0、用户程序构建和打包路径对初始栈、退出 syscall、freestanding 头文件和静态 ELF64 约束的依赖。
- [x] 1.5 记录当前低层不变假设：`int 0x80` vector、寄存器顺序、IDT gate 分离、TSS/RSP0、CR3 切换、用户栈布局、linker 地址和磁盘镜像布局。

## 2. ABI 单一来源

- [x] 2.1 确定 syscall number、errno、基础用户态 ABI 类型和 wrapper 所需常量的单一来源或一致性生成路径。
- [x] 2.2 调整 kernel dispatcher 与 userland wrapper，使两侧消费同一 ABI 来源或具备可审查的一致性检查。
- [x] 2.3 确保 kernel syscall 失败继续通过 `rax` 返回确定性负 errno，userland wrapper 继续翻译为正 `errno` 和文档化失败哨兵。
- [x] 2.4 保持现有 `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` 约定和 `int 0x80` 入口语义不变，不引入 `syscall/sysret` 或第二 backend。
- [x] 2.5 新增源码级 syscall wrapper contract 检查，验证 raw syscall primitive 的寄存器绑定、参数顺序和 `rcx`/`r11`/`memory` clobber。

## 3. 用户态边界

- [x] 3.1 收敛用户态公共头文件，只暴露当前 bounded subset 已实现且有规格约束的 constants、types、wrappers 和 libc declarations。
- [x] 3.2 移除或隐藏用户程序不应包含的 kernel-private、backend-private、scheduler、VFS、interrupt、memory-management 实现头依赖。
- [x] 3.3 检查 crt0 只依赖文档化初始用户栈、C 调用约定和稳定 exit syscall ABI，不依赖未文档化 ring3 entry 状态。
- [x] 3.4 检查 bundled user programs 通过 libc wrappers 或共享 ABI 常量发起系统调用，不在程序内维护漂移的 syscall number 表。
- [x] 3.5 对已使用但规格不足的公共声明，先补充 OpenSpec 和文档，再决定保留、移至内部头、通过 umbrella 兼容导出或隐藏。

## 4. 文档同步

- [x] 4.1 更新既有 `docs/en/arch/syscall-entry.md` 与 `docs/en/arch/userland-runtime.md`，不新增独立 `user-abi.md`，并明确 bounded subset 与非目标。
- [x] 4.2 同步更新 `docs/zh/arch/syscall-entry.md` 与 `docs/zh/arch/userland-runtime.md`，确保英中描述中的 syscall vector、寄存器顺序、errno、wrapper 和 backend 事实一致。
- [x] 4.3 确认文档不宣称完整 POSIX libc、动态链接、完整 job control、SMP、UEFI、第二 ISA 或 runtime-backend parity。
- [x] 4.4 保持 `roadmap.md` 为项目规划级别，不加入具体入口点、命令、marker、源码细节或 archive/version index。

## 5. 验证

- [x] 5.1 执行 `openspec status --change "harden-syscall-user-abi-boundary"` 并记录结果。
- [x] 5.2 执行 targeted consistency search，覆盖 syscall number、errno、`int 0x80`、寄存器约定、用户 wrapper 和公共头文件声明。
- [x] 5.3 运行新增的 syscall wrapper source-contract 检查，并确认它能覆盖 `r10`、`r8`、`r9` 参数绑定和 `rcx`/`r11`/`memory` clobber。
- [x] 5.4 若修改 C/C++ 源码或头文件，执行最窄 xmake 构建检查，并记录 `x86_64-elf-gcc`/`x86_64-elf-g++` 可用性。
- [x] 5.5 若修改 C++ 源码、C++ 头或 C++ 构建配置，执行接近交叉 GCC 环境的 clang/clangd 辅助检查，或记录工具不可用及剩余风险。
- [x] 5.6 若修改 syscall dispatcher、user wrapper、crt0、user entry 或用户程序构建行为，在环境支持时运行匹配的 QEMU headless syscall/userland smoke；若无法运行，记录缺失工具或配置与剩余风险。
- [x] 5.7 若修改 Python helper，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若未修改 Python 文件，明确该项不适用。

## 本次执行记录

- ABI 事实：`include/bigos/syscall.h` 作为内核 syscall number 与寄存器契约来源；`include/bigos/errno.h` 作为内核正 errno 来源；用户态 `user/libc/include/sys_nr.h` 与 `user/libc/include/errno.h` 保持 plain C 镜像，并由 `tests/test_syscall_entry_source.py` 校验一致性。
- 重复定义：用户态镜像头与 `user/crt0/crt0.s` 中 `SYS_EXIT` 汇编常量保留为 freestanding/assembly 可消费形式；新增源码级检查约束它们与共享 ABI 事实一致。
- 声明归类：`strchr`、`wait_status`、`bigos_readdir`、`brk_raw`、`mmap_anon`、`time_now`、`get_tick` 归为当前 public bounded ABI/helper；raw `syscall0`-`syscall6` 归为 compatibility umbrella export 与 BigOS-specific low-level ABI helper，非 POSIX `syscall(2)` 兼容。
- 头文件边界：`user/` 未发现直接 include kernel-private、backend-private、scheduler、VFS、interrupt 或 memory-management 实现头；bundled C programs 通过 `libc.h`/细粒度用户头与 libc wrapper 消费能力。
- 构建与低层假设：保留 `int 0x80` vector、`rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` 顺序、DPL=3 syscall gate、no-EOI syscall path、TSS/RSP0、CR3 切换语义、用户栈布局、`user/link.lds` 静态 ELF64 约束和 Legacy BIOS/MBR/exFAT 镜像布局。
- 文档同步：更新 `docs/en/arch/syscall-entry.md`、`docs/en/arch/userland-runtime.md` 及 `docs/zh` 镜像；未新增独立 `user-abi.md`，未修改 `roadmap.md`。
- 验证：`openspec status --change "harden-syscall-user-abi-boundary"` 通过，显示 4/4 artifacts complete。
- 验证：targeted consistency search 覆盖 syscall number、errno、`int $0x80`、寄存器约定、raw wrapper、公共头文件声明和双语文档，结果与预期 ABI 事实一致。
- 验证：`uv run pytest tests/test_syscall_entry_source.py` 通过，`16 passed`，覆盖新增 raw syscall primitive register/clobber contract。
- 验证：`command -v x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-as`、`x86_64-elf-ld`、`qemu-system-x86_64` 均可用；`xmake build user-init-elf` 通过。
- 验证：`xmake f --userland_smoke=y` 后运行 `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-smoke.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED` 通过，观测到 `BIGOS_USERLAND_PASSED`；随后已执行 `xmake f --userland_smoke=n` 恢复默认关闭。
- 验证：未修改 C++ 源码、C++ 头或 C++ 构建配置，clang/clangd 辅助检查不适用；未修改 Python helper，`ruff`/`pyright` helper 验证不适用。编辑器 diagnostics 返回空列表。
