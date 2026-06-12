## 简单 C 程序基线验证记录

### 已通过检查

- `xmake build user-init-elf`
  - 结果：通过。
  - 覆盖：默认打包用户程序与显式 `userland_smoke` 下的 `/bin/smoke/*` 探针均通过现有 `crt0 + libc + -nostdlib -static` ELF64 构建路径链接，并保持 64 KiB user-ELF 上限。
- `uv run pytest tests/test_user_c_baseline_source.py -q`
  - 结果：`3 passed`。
  - 覆盖：简单 C 程序基线探针打包、探针源码行为类别、`userland_smoke` 直接执行与 shell 执行路径。
- `uv run pytest tests/test_user_c_baseline_source.py tests/test_boot_debug.py -q`
  - 结果：`36 passed`。
  - 覆盖：simple C program baseline source checks 与 runtime smoke matrix/tooling 回归。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/stage21-userland.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 40`
  - 结果：通过，串口 marker 观察到 `BIGOS_USERLAND_PASSED`。
  - 覆盖：QEMU headless 构建 kernel、boot artifacts、user ELFs、raw image，并运行默认关闭 `userland_smoke`。该 smoke 直接执行 `/bin/smoke/*` 探针并通过 `/rw/smoke_*.txt` 记录验证参数、环境、stdout/stderr、`errno`、退出码请求；同时通过 `/bin/sh` 运行非零退出探针后继续执行参数探针。
- `uv run pyright`
  - 结果：通过，`0 errors, 0 warnings, 0 informations`。

### 已运行但存在无关阻塞

- `uv run ruff check`
  - 结果：失败。
  - 原因：既有文件 `tests/test_fd_vfs_shell_source.py` 存在 `Q003` quote-style 诊断；该文件不是本 change 修改范围。
  - 替代检查：本 change 新增测试文件已通过 targeted pytest；未修改该既有文件以避免无关变更。
- `uv run ruff format --check`
  - 结果：失败。
  - 原因：既有文件 `tests/test_fd_vfs_shell_source.py` 会被格式化；该文件不是本 change 修改范围。
  - 替代检查：本 change Python 改动通过 targeted pytest 与 pyright；未修改该既有文件以避免无关变更。
- `uv run pytest`
  - 结果：`231 passed, 1 failed`。
  - 原因：`tests/test_source_root_layout.py::test_targeted_old_active_paths_are_absent_outside_current_change` 在既有归档文件 `openspec/changes/archive/2026-06-12-interactive-console-usability/validation.md` 中发现旧路径字符串 `src/kernel`；该归档文件不是本 change 修改范围。
  - 替代检查：simple C program baseline targeted pytest、`test_boot_debug.py`、`xmake build user-init-elf` 与 QEMU headless `BIGOS_USERLAND_PASSED` 均通过。

### IDE 诊断

- `GetDiagnostics`（全局）仍报告既有 clang include/config 诊断，路径位于 `src/kernel/sched/sched.cc` 与 `src/kernel/proc/proc.cc`，例如缺少 `../../mm/memdef.h`、`../../mm/buddy.h` 和 `PAGE_SIZE` 未声明；这些文件不是本 change 修改范围。
- `GetDiagnostics`（`user/smoke/userland_smoke.c`）结果为空。

### 未执行 / 跳过

- 图形 QEMU 手工交互验证：跳过。
  - 原因：当前 API 会话没有图形窗口观察和键盘注入能力。
  - 替代检查：QEMU headless `BIGOS_USERLAND_PASSED`、source checks、构建检查。
  - 剩余风险：仍需在可交互 emulator 环境中人工确认 VGA 文本控制台上外部程序 stdout/stderr 与 shell 继续运行的视觉效果。
- Bochs 交叉验证：跳过。
  - 原因：本 change 未修改 boot、real/protected/long-mode transition、IRQ、timer、ATA PIO 或 port IO 行为；已使用 QEMU headless 验证用户态 runtime 行为。
  - 剩余风险：Bochs 上的显示/输入可观察性仍未由本次自动化运行证明。

### 契约确认

- 未新增 kernel syscall。
- 未修改 `int 0x80` syscall vector、syscall number、寄存器 ABI、CR3 切换、页表布局、boot layout、MBR/exFAT 磁盘布局或 linker address。
- simple C program baseline emulator-dependent runtime smoke 仍通过 `userland_smoke` 显式开关启用，默认构建不强制运行 QEMU/Bochs。
