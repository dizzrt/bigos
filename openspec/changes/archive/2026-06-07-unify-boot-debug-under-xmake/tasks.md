## 1. xmake 构建图迁移

- [x] 1.1 盘点根 `Makefile`、`kernel/arch/x86/boot/Makefile`、`xmake.lua` 和 `tools/boot_debug.py` 的当前职责，记录需要迁移的命令、参数、输出路径和大小限制。
- [x] 1.2 在 xmake 配置中新增 MBR、DBR、extended DBR 和 `boot.bin` 的构建规则，保持现有 `x86_64-elf-as`、`x86_64-elf-gcc`、`x86_64-elf-ld` 参数、entry symbol、`-Ttext` 地址和 `--oformat binary` 语义。
- [x] 1.3 确认 boot-stage artifacts 仍输出到 `build/bin/x86/boot/` 或等价文档化路径，并保留 `mbr.bin`、`dbr.bin`、`exdbr.bin`、`boot.bin` 文件名。
- [x] 1.4 为 boot-stage artifacts 增加构建后大小检查，保持 `512`、`512`、`4096` 和 `524288` bytes 上限。
- [x] 1.5 确认 `xmake` 无参默认仍只构建 `kernel`，不因新增 boot/debug target 改变默认构建行为。

## 2. xmake 调试入口

- [x] 2.1 新增 `bochs-sdl2` runnable target 或等价 xmake run 入口，使 `xmake run bochs-sdl2` 串起 kernel build、boot artifact build、raw image 生成、Bochs SDL2 config 生成和 Bochs 启动。
- [x] 2.2 新增 `bochs` runnable target 或等价 xmake run 入口，作为非 SDL2 Bochs 后备入口，并复用同一套构建、镜像生成和 Bochs 启动流程。
- [x] 2.3 确保 `xmake run bochs-sdl2` 和 `xmake run bochs` 使用当前 `.xmake` 保存的配置构建 kernel，不在 run 阶段执行会重置未列出 smoke 开关的固定 `xmake f` 命令。
- [x] 2.4 确保 `xmake f --user_program_smoke=y --syscall_smoke=y` 后运行 `xmake run bochs-sdl2` 会构建同时包含 `BIGOS_USER_PROGRAM_SMOKE` 和 `BIGOS_SYSCALL_SMOKE` 的 kernel。
- [x] 2.5 生成 `bochs-sdl2` Bochs 配置时追加 SDL2 display 配置，生成 `bochs` Bochs 配置时不强制注入 SDL2 display，并保持 raw image、bochsrc 和 serial log 等生成产物位于 `build/` 或文档化输出目录。
- [x] 2.6 明确 Bochs、SDL2 display 或 cross toolchain 缺失时的失败信息，避免静默启动无效配置或继续使用 stale artifacts。
- [x] 2.7 确认 `bochs-sdl2` 和 `bochs` run target 不提供稳定 `--no-launch` run argument，离线 image 生成继续通过 Python helper 路径完成。

## 3. boot_debug.py 职责调整

- [x] 3.1 将 `tools/boot_debug.py` 的 kernel 构建逻辑调整为不再固定执行 `xmake f --mm_self_test=... --user_program_smoke=...` 并清除其它 smoke 配置。
- [x] 3.2 保留或重构 `tools/boot_debug.py` 的 raw image 生成、image validation、Bochs config 渲染、Bochs launch 和 serial-marker bounded smoke 能力。
- [x] 3.3 让 xmake 调试入口以“消费已构建 artifacts”的方式复用 Python helper，避免 xmake 与 Python helper 互相递归调用构建配置。
- [x] 3.4 删除 `--memory-self-test`、`--user-program-smoke` 或等价 smoke 快捷参数，要求用户完全通过 `xmake f` 配置 smoke 开关。
- [x] 3.5 保留 `tools/boot_debug.py` 的 no-launch 离线 image 生成能力，作为 run target 不启动 Bochs 的替代路径。

## 4. 移除 Makefile 入口

- [x] 4.1 删除根 `Makefile` 中的 `run`、`boot-debug`、`boot-debug-gui` 和 `boot-debug-user-gui` 包装入口。
- [x] 4.2 删除 `kernel/arch/x86/boot/Makefile`，确保 boot-stage artifacts 已完全由 xmake 构建。
- [x] 4.3 搜索并清理测试、文档、OpenSpec 当前规范和开发指南中的 active `make boot-debug`、`boot-debug-gui`、boot Makefile 构建引用。
- [x] 4.4 保留历史 archive 中的语义记录时，必要处添加迁移说明，避免把历史验证结果误改为新入口。

## 5. 文档与测试更新

- [x] 5.1 更新 `AGENTS.md` 的 Build And Run 和验证命令，使用 `xmake`、`xmake f ...`、`xmake run bochs-sdl2`、`xmake run bochs` 和必要的 `uv run python tools/boot_debug.py` 辅助路径。
- [x] 5.2 更新 `docs/en` 中涉及 `make boot-debug`、`tools/boot_debug.py` 自动 smoke 配置、UEFI/Legacy 调试矩阵的说明。
- [x] 5.3 同步更新 `docs/zh` 对应 Markdown 路径，保持中英文文档结构和语义一致。
- [x] 5.4 更新 `tests/test_boot_debug.py`，覆盖 `boot_debug.py` 不再清掉其它 xmake smoke 配置、不再接受 smoke 快捷参数、raw image 生成仍可独立校验、Bochs config 可注入 SDL2 display。
- [x] 5.5 更新源码级测试中对 Makefile、boot Makefile、`make boot-debug`、Python smoke 快捷参数或固定 `xmake f` 参数的断言。
- [x] 5.6 增加或调整测试，验证 xmake 配置中存在 `bochs-sdl2` 和 `bochs` 调试入口、boot artifact 构建规则和关键大小检查。

## 6. 验证

- [x] 6.1 运行 `xmake`，确认默认 kernel 构建通过；如缺少 `x86_64-elf-*` 工具链，记录缺失工具和剩余风险。
- [x] 6.2 运行 boot artifact 构建路径，确认 `mbr.bin`、`dbr.bin`、`exdbr.bin`、`boot.bin` 生成且大小限制通过。
- [x] 6.3 运行 `xmake f --user_program_smoke=y --syscall_smoke=y` 后执行 `xmake run bochs-sdl2`，确认生成的 image 和 bochsrc 使用当前 xmake 配置；若 Bochs/SDL2 不可用，记录不可用原因。
- [x] 6.4 运行 `xmake run bochs` 或在 Bochs 不可用时检查对应 no-SDL2 generated config 路径，确认非 SDL2 后备入口不强制注入 SDL2 display。
- [x] 6.5 运行 `uv run python tools/boot_debug.py run --no-launch` 或迁移后的等价 helper 命令，确认 raw image 生成与 image validation 仍通过。
- [x] 6.6 对 Python 修改运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 或工具不可用，记录 blocker 而不是静默改用系统 Python。
- [x] 6.7 对 xmake/C++ 构建配置修改运行可行的 clang/clangd 辅助检查，区分历史诊断、当前变更新增诊断和 freestanding/toolchain false positive。
- [x] 6.8 运行 `openspec validate --all`，确认 proposal、design、spec delta 和 tasks 结构有效。
- [x] 6.9 记录最终验证结果，包括已通过检查、未运行检查的原因、Bochs runtime 剩余风险和 smoke 组合 runtime marker 的已知限制。


## 验证记录

- `xmake`：通过；验证默认构建仍只构建 `kernel` target。
- `xmake build boot-artifacts`：通过；生成 `mbr.bin` 512 bytes、`dbr.bin` 512 bytes、`exdbr.bin` 890 bytes、`boot.bin` 10936 bytes，均满足上限。
- `xmake f --user_program_smoke=y --syscall_smoke=y` + `xmake build kernel`：通过；构建日志包含 `kernel/core/proc/**`，验证组合 smoke 配置被当前 xmake 配置保留并参与构建。验证结束后已将这两个本地开关恢复为默认关闭。
- `xmake run bochs-sdl2`：到达 kernel/boot artifact 构建、raw image 生成、SDL2 bochsrc 生成和 Bochs launch 阶段；为避免交互式 emulator 后台挂起，启动后手动停止。生成配置包含 `display_library: sdl2`。
- `xmake run bochs`：到达 kernel/boot artifact 构建、raw image 生成、非 SDL2 bochsrc 生成和 Bochs launch 阶段；为避免交互式 emulator 后台挂起，启动后手动停止。生成配置包含 raw image `ata0-master` 且不包含 `display_library: sdl2`。
- `uv run python tools/boot_debug.py run --no-launch`：通过；默认按当前 xmake 配置构建 `kernel` 与 `boot-artifacts`，生成并校验 `build/test/os.raw`。
- `uv run python tools/boot_debug.py run --skip-build --no-launch --bochs-extra 'display_library: sdl2'` 与无 SDL2 extra 的 no-launch 路径：通过；确认 helper 可消费已构建 artifacts，且 generated bochsrc 可注入/不注入 SDL2 display。
- `uv run pytest`：通过，111 passed。
- `uv run pyright`：通过，0 errors；工具报告 `bigos.py` 不存在和 pyright 有新版本的提示，但不产生诊断。
- `uv run ruff check tools/boot_debug.py tests/test_boot_debug.py tests/test_memory_correctness_source.py`：未通过，命中 `tests/test_memory_correctness_source.py:167` 的既有 `E501` 长行，不是本次新增断言。
- `uv run ruff format --check tools/boot_debug.py tests/test_boot_debug.py tests/test_memory_correctness_source.py`：未通过，提示 `tests/test_memory_correctness_source.py` 需要格式化；该文件存在历史格式差异，本次未批量重排以避免扩大无关 diff。
- `openspec validate --all`：通过，25 passed。
- Bochs runtime 剩余风险：本次验证确认两个 run target 都能启动到 Bochs launch 阶段；没有让交互式 emulator 长时间运行到 kernel runtime marker，因此 `BIGOS_USER_PROGRAM_SMOKE` 与 `BIGOS_SYSCALL_SMOKE` 组合的 runtime marker 顺序仍依赖后续人工/有界 smoke 验证。
