## 1. Package 骨架与共享基础

- [x] 1.1 新建 `tools/bigosdev/` package，包含 `__init__.py`、`__main__.py`、`cli.py`、`config.py`、`errors.py`、`process.py`、`artifacts.py`、`font_asset.py` 以及 `image/`、`emulator/`、`smoke/` 子包
- [x] 1.2 将 `StageError`、路径常量、boot mode/emulator/display 枚举、默认 image/log 路径、artifact size bound 和 smoke marker 常量迁移到共享模块
- [x] 1.3 将 `run_command()`、工具可用性检查、display 校验、日志路径约束、process group 终止和通用文件校验迁移到 `process.py`
- [x] 1.4 将 xmake artifact 构建、`PreparedArtifacts`、artifact 收集和 font asset 生成迁移到 `artifacts.py`/`font_asset.py`

## 2. 镜像模块迁移

- [x] 2.1 将 exFAT checksum、目录项生成、目录解析、partition/boot-region 读取和文件查找逻辑迁移到 `tools/bigosdev/image/exfat.py`
- [x] 2.2 将 Legacy `ImageLayout`、MBR/exFAT raw image 创建、`validate_image()` 和相关布局常量迁移到 `tools/bigosdev/image/legacy.py`
- [x] 2.3 将 UEFI ESP/FAT image 创建、mtools copy、OVMF path 解析、vars copy、UEFI root image 路径和 `validate_uefi_image()` 迁移到 `tools/bigosdev/image/uefi.py`
- [x] 2.4 将 persistent `/rw` 测试盘创建迁移到 `tools/bigosdev/image/persistent.py`
- [x] 2.5 将 `tools/install.py` 的 MBR/DBR/exDBR/`boot.bin` 受限覆盖写入能力迁移到 `tools/bigosdev/image/patch.py`，移除全局可变路径状态，并保持 unsupported-layout 失败边界

## 3. Emulator 与 run 子命令

- [x] 3.1 将 QEMU Legacy/UEFI command 生成、extra args 解析、GDB stub 支持和 serial marker 等待迁移到 `tools/bigosdev/emulator/qemu.py`
- [x] 3.2 将 Bochs config 渲染、锁清理、SMP 拒绝诊断、launch 和 serial marker 等待迁移到 `tools/bigosdev/emulator/bochs.py`
- [x] 3.3 在 `cli.py` 中实现 `run`、`image create`、`image validate`、`image patch` 子命令，保持现有参数语义并新增 package 入口帮助信息
- [x] 3.4 更新 `xmake/run_targets.lua`，将 `tools/boot_debug.py run` 调用迁移为 `python3 -m tools.bigosdev run`，并保持 `--` 后 target arguments 原样转发

## 4. Runtime smoke 迁移

- [x] 4.1 将 `SMOKE_OPTIONS`、`RuntimeSmokeCase` 和 `RUNTIME_SMOKE_MATRIX` 迁移到 `tools/bigosdev/smoke/cases.py`
- [x] 4.2 将 runtime smoke case 选择、xmake 配置、extra image 创建、modern storage preflight 和单 case 执行迁移到 `tools/bigosdev/smoke/runner.py`
- [x] 4.3 将 `RuntimeSmokeResult`、tool availability 记录、Markdown artifact 格式化和写入迁移到 `tools/bigosdev/smoke/report.py`
- [x] 4.4 在 CLI 中实现 `smoke matrix` 子命令，保持默认 `logs/runtime-smoke-validation.md`、`logs/runtime-smoke/`、case id、`--keep-going` 和 `--record-bochs` 语义

## 5. 删除旧入口与测试迁移

- [x] 5.1 删除 `tools/boot_debug.py`，不提供兼容 shim，并清理项目内 active 调用
- [x] 5.2 删除 `tools/install.py`，不提供兼容 shim，并确认其 patch 能力已由 `tools.bigosdev image patch` 覆盖
- [x] 5.3 将 `tests/test_boot_debug.py` 拆分或重命名为面向 `tools.bigosdev` package 的测试，覆盖 parser、路径约束、镜像 layout、UEFI image、font asset、emulator command、marker wait、runtime smoke 和 image patch
- [x] 5.4 更新 source-level tests 中对 `tools/boot_debug.py` 内容的字符串断言，使其检查 `tools/bigosdev/**` 中的 smoke case、modern storage device 参数和 helper 入口
- [x] 5.5 更新 `pyrightconfig.json`、ruff/pytest 相关目标，使 Python 检查覆盖 `tools/bigosdev/**` 并不再引用删除的脚本

## 6. 文档与 OpenSpec 基础规范清理

- [x] 6.1 更新 `README.md` 和 `README-zh.md`，将工具说明、启动示例、runtime smoke 示例和 image patch 示例迁移到 `uv run python -m tools.bigosdev ...`
- [x] 6.2 更新 `docs/en/**` 与 `docs/zh/**` 中活跃 helper 路径，保持中英文镜像文档同步；归档历史命令仅在明确为历史记录时保留
- [x] 6.3 更新 `AGENTS.md` 中的 Python helper、QEMU/Bochs smoke 和验证命令示例，使用 `tools.bigosdev`
- [x] 6.4 更新基础 OpenSpec specs：`one-command-boot-debug`、`runtime-smoke-validation`、`x86-bootloader-hardening`、`source-layout-organization`、`project-quality-assurance`，归档后不得继续把 `tools/boot_debug.py` 或 `tools/install.py` 描述为 active supported entry
- [x] 6.5 使用 targeted search 清理 active 文档和 specs 中的 `tools/boot_debug.py`、`tools/install.py`、`boot_debug.py`、`install.py --with-boot`、`runtime-smoke-matrix` 旧入口残留，并记录允许保留的 archive 历史命中

## 7. 验证

- [x] 7.1 运行 `uv run python -m tools.bigosdev --help`、`uv run python -m tools.bigosdev image --help` 和 `uv run python -m tools.bigosdev smoke --help`
- [x] 7.2 运行 `uv run pytest tests/test_bigosdev*.py` 或迁移后的等价 Python 测试集合
- [x] 7.3 运行 `uv run ruff check tools/bigosdev tests` 和 `uv run ruff format --check tools/bigosdev tests`
- [x] 7.4 运行 `uv run pyright tools/bigosdev tests`，如 pyright 配置使用项目文件则同步运行对应项目检查
- [x] 7.5 运行 `uv run python -m tools.bigosdev image create --boot-mode legacy --emulator qemu --display none --no-launch` 或等价 no-launch 入口，验证 Legacy image 生成和校验
- [x] 7.6 在工具链和 emulator 可用时运行一个 QEMU headless marker 验证，例如 `uv run python -m tools.bigosdev run --boot-mode uefi --emulator qemu --display none --expect-serial-marker BIGOS_USER_EXEC`；若 QEMU、OVMF、mtools、cross-toolchain 或本地配置不可用，明确记录 blocker 和剩余风险
- [x] 7.7 针对 `image patch` 构造或复用测试镜像，验证 `--with-mbr`、`--with-dbr`、`--with-exdbr`、`--with-boot` 成功路径和 unsupported-layout 失败路径
- [x] 7.8 运行 `openspec validate introduce-bigosdev-tooling --strict`，并在实现完成后运行相关基础 specs 的状态/校验检查
