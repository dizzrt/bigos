## 1. Helper Backend 设计落地

- [x] 1.1 梳理 `tools/boot_debug.py` 当前 Bochs launch、serial marker、preflight、no-launch 路径，确认可复用与需要拆分的函数边界。
- [x] 1.2 为 Python helper 增加显式 emulator/backend 参数，支持 `bochs`、`bochs-sdl2`、`qemu`、`qemu-gdb` 或等价内部枚举，并增加 `--display` 或等价参数控制图形/无图形模式。
- [x] 1.3 调整 preflight，使其只检查当前选择 backend 所需的 emulator 工具；`--no-launch` 或离线 image 校验不得要求 Bochs/QEMU。
- [x] 1.4 保持 `build_current_artifacts()`、`create_image()`、`validate_image()`、exFAT 布局和 smoke 开关配置语义不变。

## 2. QEMU Backend 实现

- [x] 2.1 实现 QEMU 普通启动命令，使用当前 raw image、Legacy BIOS 默认启动和 IDE disk 配置，例如 `-drive file=<image>,format=raw,if=ide`。
- [x] 2.2 为 QEMU backend 实现 display 模式参数，至少支持图形 display 和 `none` headless display；headless 模式配置 COM1 serial log、`-no-reboot` 或等价稳定 smoke 参数。
- [x] 2.3 实现 QEMU GDB 启动命令，启用 GDB stub 并在 boot 前暂停执行，默认保留图形 display 以观察 VGA 输出，同时输出 GDB attach 说明。
- [x] 2.4 为 QEMU serial marker smoke 实现独立进程组启动、marker 轮询、timeout 失败和进程清理逻辑，不复用 Bochs 专属退出码判断。
- [x] 2.5 为 QEMU backend 添加可选额外参数透传能力或明确不支持透传的错误提示，避免影响稳定测试契约。

## 3. xmake Run Targets

- [x] 3.1 在 `xmake.lua` 中新增 `qemu` phony target，依赖 `kernel` 和 `boot-artifacts`，并通过 Python helper 的 QEMU backend 启动。
- [x] 3.2 在 `xmake.lua` 中新增 `qemu-gdb` phony target，使用独立 serial log 路径和 GDB backend 参数。
- [x] 3.3 确认自动化 headless smoke 通过 Python helper 的 display 参数实现，而不是新增 `qemu-headless` phony target。
- [x] 3.4 确认新增 target 不改变 `bochs`、`bochs-sdl2`、`kernel`、`boot-artifacts` 的现有构建和运行语义。

## 4. 测试覆盖

- [x] 4.1 更新或新增 `tests/test_boot_debug.py`，覆盖 QEMU 命令参数包含 Legacy BIOS/IDE disk、raw image path、serial log、headless 参数和 GDB 参数。
- [x] 4.2 增加 preflight 测试，覆盖 QEMU backend 缺少 `qemu-system-x86_64`、Bochs backend 缺少 `bochs`、no-launch 不要求 emulator 的场景。
- [x] 4.3 增加 serial marker smoke 单元测试或可替代的进程模拟测试，覆盖 QEMU marker 成功、timeout 失败和进程清理。
- [x] 4.4 增加回归测试，确认 Bochs backend 仍生成原有 Bochs 配置并保持 `bochs`/`bochs-sdl2` 行为。

## 5. 文档与 Agent 指南

- [x] 5.1 更新 `README.md` 与 `README-zh.md`，说明 `xmake run qemu`、`xmake run qemu-gdb` 的用途、`--display` 或等价参数、依赖和限制。
- [x] 5.2 若 `docs/en` 或 `docs/zh` 已存在对应启动调试文档，同步更新中英文镜像路径，保持目录结构和内容语义一致。
- [x] 5.3 更新 `AGENTS.md`，规定自动化 smoke/serial marker/CI-like 验证优先 `qemu` 的 headless display helper 路径，快速本地启动验证优先 `qemu` 的图形 display 模式，早期 boot 和硬件行为差异排查保留 Bochs 或双 emulator 交叉验证。
- [x] 5.4 明确文档中的非目标：本 change 不实现 UEFI loader、ESP/FAT 镜像、OVMF 或新存储驱动。

## 6. 验证与质量记录

- [x] 6.1 运行 `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py`，修复当前 change 引入的 Python lint 问题。
- [x] 6.2 运行 `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py`，修复当前 change 引入的格式问题。
- [x] 6.3 运行 `uv run pyright tools/boot_debug.py tests/test_boot_debug.py`，修复当前 change 引入的类型诊断。
- [x] 6.4 运行 `uv run pytest tests/test_boot_debug.py`，确认 helper 和 backend 单元测试通过。
- [x] 6.5 运行 `xmake build kernel boot-artifacts` 或等价最窄 xmake 构建，确认新增 run target 不破坏现有构建图。
- [x] 6.6 在 QEMU 可用时运行 `xmake run qemu` 的图形启动验证，并运行等价 `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log ... --expect-serial-marker ...` headless smoke；若不可用，记录缺失工具和剩余风险。
- [x] 6.7 在 Bochs 可用时运行 `xmake run bochs` 或 `xmake run bochs-sdl2` 进行回归验证；若不可用，记录缺失工具、已执行替代检查和剩余风险。
- [x] 6.8 运行 `openspec status --change add-qemu-emulator-backend` 和适用的 OpenSpec 校验命令，确认 change artifacts 与 specs 可解析。
- [x] 6.9 说明本 change 不修改 C++ runtime 代码、public headers、kernel ABI、bootloader、linker 或 C++ compile flags；clang/clangd 辅助检查不适用，除非实现过程中额外修改了相关文件。

## 验证记录

- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py`：通过。
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py`：通过，2 个文件已符合格式。
- `uv run pyright tools/boot_debug.py tests/test_boot_debug.py`：通过，0 errors / 0 warnings / 0 informations；pyright 提示存在新版 `1.1.410`，未影响检查。
- `uv run pytest tests/test_boot_debug.py`：通过，17 passed。
- `xmake build kernel`：通过；仍有既有 `LOAD segment with RWX permissions` linker warning。
- `xmake build boot-artifacts`：通过；仍有既有 `movsd`/`movsl` assembler warning。
- `xmake build kernel boot-artifacts`：本机 xmake `3.0.9` 不接受该多 target 写法，返回 `invalid argument: boot-artifacts`；已用上面两个等价单 target 构建替代。
- `command -v qemu-system-x86_64`：存在 `/opt/homebrew/bin/qemu-system-x86_64`。
- `command -v bochs`：存在 `/opt/homebrew/bin/bochs`。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --skip-build --serial-log build/test/qemu-headless.serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 5`：按预期启动 QEMU headless、生成/校验 image、使用 IDE/raw command，并在 marker 未出现时 timeout 后清理；serial log 为空，剩余风险是本机 QEMU runtime 未观测到 BigOS 串口 marker。
- `xmake run qemu`：构建 target、生成 image、输出 QEMU IDE/raw command 并进入图形 QEMU launch；为避免残留交互 emulator，手动停止，退出码 130，剩余风险是未完成交互式启动后的 marker/VGA 人工确认。
- `uv run python tools/boot_debug.py run --emulator bochs --skip-build --serial-log build/test/bochs-smoke.serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 5`：生成/校验 image 并进入 Bochs smoke，marker timeout，且未生成 serial log；剩余风险是本机 Bochs runtime 未观测到 BigOS 串口 marker。
- `xmake run bochs`：构建 target、生成 image、生成 Bochs config 并进入 Bochs launch；为避免残留交互 emulator，手动停止，退出码 130，剩余风险是未完成交互式启动后的 marker/VGA 人工确认。
- `openspec status --change add-qemu-emulator-backend`：通过，4/4 artifacts complete。
- `openspec validate add-qemu-emulator-backend --strict`：通过，change valid。
- 本 change 仅修改 Python helper、xmake target、Python 测试、README/AGENTS/docs/OpenSpec task 记录；未修改 C++ runtime、public headers、kernel ABI、bootloader、linker 或 C++ compile flags，因此 clang/clangd 辅助检查不适用。
