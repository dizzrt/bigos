# Validation Notes

## 已通过检查

- `GetDiagnostics`: 无 VS Code/clangd 诊断。
- `xmake`: 默认配置构建通过。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/terminal-abstraction-default.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`: QEMU headless 默认 init/shell 路径观察到 `BIGOS_USER_EXEC`。
- `xmake f --userland_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/terminal-abstraction-userland.serial.log --expect-serial-marker BIGOS_USERLAND_PASSED --smoke-timeout 40`: userland smoke 观察到 `BIGOS_USERLAND_PASSED`。
- `xmake f --userland_smoke=n`: 已将 userland smoke 配置恢复为默认关闭。
- `xmake`: 恢复默认 smoke 配置后构建通过。
- `uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/terminal-abstraction-bochs.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`: Bochs headless 观察到 `BIGOS_USER_EXEC`。

## 工具与环境

- `uv`: 可用，路径为 `/opt/homebrew/bin/uv`。
- `xmake`: 可用，路径为 `/opt/homebrew/bin/xmake`。
- `x86_64-elf-gcc`: 可用，路径为 `/usr/local/bin/cross_compiler/bin/x86_64-elf-gcc`。
- `x86_64-elf-g++`: 可用，路径为 `/usr/local/bin/cross_compiler/bin/x86_64-elf-g++`。
- `qemu-system-x86_64`: 可用，路径为 `/opt/homebrew/bin/qemu-system-x86_64`。
- `bochs`: 可用，路径为 `/opt/homebrew/bin/bochs`。

## 跳过或阻塞检查

- 图形 QEMU/Bochs 手工键盘输入验证未执行：当前会话没有可记录的人工显示/键盘输入过程；已用 QEMU headless、Bochs headless、构建和源级检查替代。
- emulator scancode injection 未执行：当前 helper 验证只使用串口 marker，不注入 `Ctrl-D`、`Ctrl-C` 或 unsupported control 序列。
- 独立 clang 命令未执行：使用 IDE/clangd 诊断与 freestanding xmake 构建作为 C++/C 静态替代检查。

## 历史诊断

- boot artifact 构建仍输出既有 assembler warning：`mbr.s` 与 `dbr_exfat.s` 中 `movsd` 被 assembler 解释为 `movsl`。该 warning 来自 boot assembly 构建路径，不是本次终端抽象改动新增。
- userland smoke wrapper 命令最终 exit code 为 `1`，原因是 zsh 中 `status` 是 readonly variable；串口日志已在该命令中观察到 `BIGOS_USERLAND_PASSED`，随后单独执行 `xmake f --userland_smoke=n` 并重新 `xmake` 通过。

## 当前变更新问题

- 未发现新增 VS Code/clangd 诊断。
- 未发现默认构建失败。
- 未发现 QEMU headless 默认 init/shell marker 缺失。
- 未发现 Bochs headless 默认 init/shell marker 缺失。

## 残余风险

- `Ctrl-D` EOF-like、`Ctrl-C` interrupt-like、unsupported control no-op 和 backspace/line feedback 的实际屏幕交互未通过人工图形会话观察；当前证据覆盖源码路径、构建、默认 headless marker、userland smoke marker 和 Bochs headless reachability。
- 新增终端抽象未实现 termios、session、job control、terminal process group、伪终端、SMP、动态链接或完整 POSIX terminal；这些仍是明确 non-goals。
