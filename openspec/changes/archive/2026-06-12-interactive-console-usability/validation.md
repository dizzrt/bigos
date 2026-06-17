## 交互控制台可用性验证记录

### 已通过

- `uv run pytest tests/test_tty_console_input_source.py tests/test_fd_vfs_shell_source.py tests/test_boot_debug.py`
  - 结果：通过，`53 passed`。
  - 覆盖：keyboard IRQ1 不直接普通输出/分配/阻塞，默认 fd `1/2` 可见 console fast path，shell prompt gating、bounded readline echo/backspace、stderr 错误恢复、boot_debug default-init 行为断言。
- `xmake`
  - 结果：通过，`build ok`。
  - 备注：`x86_64-elf-ld` 报告现有 `LOAD segment with RWX permissions` warning；本 change 未改变 linker layout。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/default-init.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，串口观察到 `BIGOS_USER_EXEC`。
  - 日志：`build/test/default-init.serial.log`。
- `uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/bochs-after-console.serial.log --no-launch` 后运行 `bochs -f build/test/bochsrc.bxrc -q` 约 5 秒
  - 结果：通过，串口日志停在 `BIGOS_USER_WRITE_SYSCALL` 与 `$ `，未继续刷 `BIGOS_PAGE_FAULT`。
  - 日志：`build/test/bochs-after-console.serial.log`。
  - 回归原因确认：默认 fd `1/2` console fast path 需要在写 VGA 前临时切回 kernel CR3，避免在 user CR3 下访问低地址 `0xb8000`。
- `GetDiagnostics`
  - 结果：`kernel/core/syscall/syscall.cc` 与 `user/sh/sh.c` 无诊断。
  - 历史诊断：workspace-wide clang 仍报告旧 `src/kernel/sched/sched.cc`、`src/kernel/proc/proc.cc` freestanding include/config 诊断，例如缺少 `../../mm/memdef.h`、`../../mm/buddy.h` 和 `PAGE_SIZE` 未声明；这些不是本 change 修改文件。
- 文档 non-goal 检查
  - 结果：`docs/en` 与 `docs/zh` 更新均以 non-goal 形式提及 POSIX terminal、termios、job control、SMP、UEFI、完整 POSIX libc 等边界，未声称完整能力。

### 工具可用性

- `uv`: `/opt/homebrew/bin/uv`
- `xmake`: `/opt/homebrew/bin/xmake`
- `x86_64-elf-gcc`: `/usr/local/bin/cross_compiler/bin/x86_64-elf-gcc`
- `x86_64-elf-g++`: `/usr/local/bin/cross_compiler/bin/x86_64-elf-g++`
- `x86_64-elf-ld`: `/usr/local/bin/cross_compiler/bin/x86_64-elf-ld`
- `qemu-system-x86_64`: `/opt/homebrew/bin/qemu-system-x86_64`
- `bochs`: `/opt/homebrew/bin/bochs`

### 已跳过 / 未自动证明

- 图形 QEMU、Bochs 或手工键盘输入验证：跳过。
  - 原因：当前 API 会话无法观察或操作本地图形 emulator 窗口，也没有接入 emulator keyboard injection。
  - 替代检查：源码级 prompt/echo/stdout/stderr 检查、默认 `xmake` 构建、QEMU headless `BIGOS_USER_EXEC` 串口 marker。
  - 剩余风险：仍需在可交互 emulator 环境中人工确认 VGA 文本控制台上 `$ ` prompt、typed input echo、backspace feedback 与命令输出的视觉效果。

### 契约确认

- 未修改 boot layout、linker address、IDT/syscall vector、CR3 切换、disk layout 或 syscall ABI number。
- 默认交互策略未新增 kernel console syscall：`/bin/sh` 通过现有 `dup()` 行为判断 fd `0/1` 是否仍为默认 console fast path；安装了 pipe/file 的描述符会抑制 prompt。
- Keyboard IRQ1 仍只执行 scancode 读取、bounded decode、TTY enqueue/wakeup；普通输入回显位于用户态 shell 的非中断消费路径。
