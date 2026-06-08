## 1. Python Helper CLI

- [x] 1.1 在 `tools/boot_debug.py` 中移除 `bochs-sdl2` emulator backend，并确保 `--emulator bochs-sdl2` 被 argparse 或 helper 校验拒绝。
- [x] 1.2 将 `--display` 改造为 backend-aware 通用参数：`bochs` 支持 `sdl2|none` 且默认 `sdl2`，`qemu`/`qemu-gdb` 支持 `graphical|none` 且默认 `graphical`。
- [x] 1.3 为 Bochs display 生成配置：`sdl2` 注入 `display_library: sdl2`，`none` 注入 Bochs no-GUI display 配置，并保留 `--bochs-extra` 对本机特殊配置的覆盖能力。
- [x] 1.4 保持 QEMU `graphical|none` 现有语义，并明确拒绝 `qemu --display sdl2`、`bochs --display graphical` 等无效组合。

## 2. xmake Entrypoints

- [x] 2.1 从 `xmake.lua` 删除 `target("bochs-sdl2")`，只保留 `bochs`、`qemu` 和 `qemu-gdb` emulator run target。
- [x] 2.2 将 `bochs` target 改为调用 `tools/boot_debug.py run --emulator bochs --skip-build --serial-log build/test/bochs.serial.log`，并默认由 helper 选择 SDL2 display。
- [x] 2.3 将 `bochs`、`qemu`、`qemu-gdb` target 改为使用 `os.execv` 或等价参数数组调用方式，避免 shell quoting 风险。
- [x] 2.4 读取并转发 `xmake run <target> -- ...` 的 target arguments 到 Python helper，确保 `--display` 等参数边界不被 `xmake.lua` 重新拆分。

## 3. Tests

- [x] 3.1 更新 `tests/test_boot_debug.py` 中的 emulator 列表、parser 和 preflight 测试，覆盖 `bochs-sdl2` 被拒绝。
- [x] 3.2 增加 Bochs display 配置测试，验证默认 `bochs` 与 `--display sdl2` 生成 SDL2 配置，`--display none` 生成 no-GUI 配置。
- [x] 3.3 增加 QEMU display 和无效组合测试，验证 `qemu --display none` 继续生成 `-display none`，`qemu --display sdl2` 与 `bochs --display graphical` 被拒绝。
- [x] 3.4 增加 xmake 参数转发相关测试或可审查验证，确认 `xmake run qemu -- --display none`、`xmake run bochs -- --display none` 会把参数传给 helper。

## 4. Documentation And Specs

- [x] 4.1 更新 `README.md` 和 `README-zh.md`，删除 `xmake run bochs-sdl2`，新增 `xmake run bochs -- --display sdl2|none` 和 `xmake run qemu -- --display none` 示例。
- [x] 4.2 更新 `AGENTS.md` 的 Build/Run、Testing/Validation 和 emulator 使用优先级说明，移除 `bochs-sdl2` 稳定入口。
- [x] 4.3 搜索并同步更新 `docs/en/**` 与 `docs/zh/**` 中的 Bochs/QEMU 调试入口说明，保持中英文镜像路径内容一致。
- [x] 4.4 更新基础 specs：`openspec/specs/one-command-boot-debug/spec.md` 和 `openspec/specs/project-quality-assurance/spec.md`，使归档后的稳定契约与本 change delta 一致。

## 5. Validation

- [x] 5.1 运行 OpenSpec 校验，确认新 change 与修改后的 requirement delta 可解析，例如 `openspec status --change unify-emulator-display-args` 和项目约定的 OpenSpec 校验命令。
- [x] 5.2 运行 Python 测试：`uv run pytest tests/test_boot_debug.py`。
- [x] 5.3 运行 Python 静态与格式检查：`uv run ruff check tools/boot_debug.py tests/test_boot_debug.py`、`uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py`、`uv run pyright tools/boot_debug.py tests/test_boot_debug.py`；如果 `uv` 或对应工具不可用，记录阻塞原因和剩余风险。
- [x] 5.4 运行最窄可行构建验证：`xmake build kernel` 和 `xmake build boot-artifacts`，确认 xmake run target 改造未破坏构建依赖。
- [x] 5.5 在 emulator 可用时运行 no-launch 或 headless 验证：`uv run python tools/boot_debug.py run --emulator qemu --display none --no-launch`，并在可行时用串口 marker 路径验证 `--display none`；若 QEMU、Bochs、cross-toolchain 或 display library 不可用，明确记录缺失工具和剩余风险。
- [x] 5.6 手动或自动验证新入口命令渲染：`xmake run qemu -- --display none`、`xmake run bochs -- --display none`、`xmake run bochs`；若本机无法启动 emulator，至少通过 helper 输出或测试替代验证参数转发。
