## Why

当前本地 boot 调试入口分散在 `Makefile`、`src/arch/x86/boot/Makefile`、`xmake.lua` 和 `tools/boot_debug.py` 之间，开发者需要混用 `make`、`xmake` 和 Python helper 才能完成构建、镜像生成和 Bochs 启动。随着 `user_program_smoke`、`syscall_smoke`、`timer_smoke` 等 xmake 开关增加，现有 `boot_debug.py` 的固定重配置会清掉未显式支持的 smoke 配置，导致组合调试不可预测。

本 change 将本地 Legacy BIOS/MBR/exFAT/Bochs 调试闭环统一到 xmake，使 `xmake f ...` 保存测试开关，`xmake run bochs-sdl2` 按当前配置构建并启动 Bochs SDL2 调试，同时提供 `xmake run bochs` 作为非 SDL2 后备入口。

## What Changes

- **BREAKING**: 移除项目级 `Makefile` 作为推荐调试入口，不再以 `make boot-debug` / `make boot-debug-gui` / `make boot-debug-user-gui` 作为稳定命令。
- **BREAKING**: 移除 boot 子目录 `Makefile` 作为 boot-stage artifact 的构建入口，将 MBR、DBR、extended DBR 和 `boot.bin` 构建纳入 `xmake.lua` 管理。
- 新增或调整 xmake runnable target，使 `xmake run bochs-sdl2` 成为 Legacy BIOS/MBR/exFAT/Bochs SDL2 的一命令调试入口，并使 `xmake run bochs` 成为非 SDL2 后备入口。
- 保持 `xmake` 无参默认构建 `kernel`；调试 target 负责按当前 xmake 配置构建 kernel、构建 boot-stage artifacts、生成 raw image 和 Bochs 配置，然后启动 Bochs。
- 保持 `xmake f --user_program_smoke=y --syscall_smoke=y` 等配置持久化语义；`xmake run bochs-sdl2` 和 `xmake run bochs` 必须使用当前已保存配置，而不是在运行阶段重置 smoke 开关。
- 调整 `tools/boot_debug.py` 职责边界，使它可继续承担 raw image、Bochs 配置、serial-marker smoke 和 no-launch 离线生成辅助能力，但不得硬编码重置 xmake smoke 配置，也不保留 smoke 快捷参数。
- 更新 `AGENTS.md`、`docs/en`、`docs/zh`、相关测试和 OpenSpec 要求，统一描述新的 xmake 调试入口。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `one-command-boot-debug`: 将稳定本地 boot debug 入口从 `make boot-debug` / Python wrapper 迁移为 xmake run target，明确 `xmake f` 保存 smoke 配置且调试入口按当前配置构建并启动。
- `source-layout-organization`: 更新构建系统职责边界，要求 boot-stage artifact 构建由主 xmake 配置管理，不再依赖 boot 子目录 Makefile。

## Impact

- 影响构建系统：`xmake.lua`、可能的 xmake 自定义 rule/target/action、boot-stage binary 输出路径和依赖关系。
- 影响调试工具：`tools/boot_debug.py` 的 kernel 构建配置逻辑、Bochs SDL2 extra 配置入口、`--no-launch`、serial-marker smoke 复用方式和旧 smoke 快捷参数移除。
- 影响删除项：根 `Makefile`、`src/arch/x86/boot/Makefile`，以及任何文档或测试中对这些入口的稳定性断言。
- 影响文档：`AGENTS.md`、`docs/en/arch/*`、`docs/zh/arch/*` 中的 `make boot-debug`、`uv run python tools/boot_debug.py run ...` 推荐路径和 UEFI/Legacy 调试矩阵描述。
- 影响测试：`tests/test_boot_debug.py` 和源码级测试中对 `boot_debug.py` 固定 `xmake f` 参数、boot Makefile、Makefile 入口的断言。
- 工具链假设：继续使用 `xmake`、`x86_64-elf-gcc`/`x86_64-elf-g++`/`x86_64-elf-ld`/`x86_64-elf-as`、Python 标准库和 Bochs；Python 验证通过 `uv run ...` 执行。
- 架构与镜像假设：仅覆盖当前 x86_64 Legacy BIOS/MBR/exFAT/Bochs 路径，不改变 kernel link address、boot handoff ABI、disk image layout、bootloader lookup 规则或内核初始化顺序。
- 非目标：不实现 QEMU 后端、不实现 UEFI/OVMF 调试入口、不改变 smoke runtime 行为、不新增用户程序加载器、不改变现有 serial marker 语义。
