## Why

`tools/boot_debug.py` 已经从早期启动调试脚本演进为覆盖构建产物、镜像生成、UEFI/Legacy 启动、QEMU/Bochs、串口 marker、runtime smoke matrix 和验证报告的综合开发工具，单文件体量过大且命名不再准确。`tools/install.py` 也只剩“修补已有 Legacy exFAT 镜像中的 boot artifact”的窄能力，日常启动链路已经不再依赖它，但该能力仍被 x86 bootloader hardening 契约约束。

本变更将这些 Python 开发工具收敛为正式的 `tools/bigosdev/` 可执行 package，建立分层模块边界，并清理旧入口、旧文档和旧 OpenSpec 契约，降低后续启动、镜像、emulator 和 smoke 验证能力的维护成本。

## What Changes

- **BREAKING**: 删除 `tools/boot_debug.py` 旧脚本入口，不提供兼容 shim；项目内所有调用、文档、测试和 OpenSpec 契约迁移到 `uv run python -m tools.bigosdev ...`。
- **BREAKING**: 删除 `tools/install.py` 旧脚本入口，不提供兼容 shim；原 `--with-mbr`、`--with-dbr`、`--with-exdbr`、`--with-boot` 能力迁移到 `tools.bigosdev image patch` 子命令。
- 新增 `tools/bigosdev/` Python package，使用 `__main__.py` 作为稳定执行入口，并按 CLI、构建产物、镜像、emulator、runtime smoke、报告生成等职责拆分模块。
- 将 Legacy MBR/exFAT raw image 创建与校验、UEFI ESP/root image 创建与校验、persistent `/rw` 测试盘创建、font asset 生成、QEMU/Bochs 启动和串口 marker 轮询迁入新的分层实现。
- 将 runtime smoke matrix、case 选择、xmake smoke 配置、工具可用性记录和 Markdown 验证报告迁入新的 smoke/report 模块。
- 更新 `xmake/run_targets.lua`，使 `xmake run bochs|qemu|qemu-legacy|qemu-gdb|qemu-uefi` 调用 `python3 -m tools.bigosdev run ...`，保持现有 target 语义和参数转发。
- 更新 Python 测试，避免继续直接 import 单个大脚本；测试改为覆盖 package 模块、CLI parser、镜像 patch、emulator 命令和 runtime smoke 行为。
- 同步清理 README、双语 docs、AGENTS、pyright 配置和相关 OpenSpec 基础 specs 中对 `tools/boot_debug.py`、`tools/install.py` 的活跃引用。

## Capabilities

### New Capabilities
- `bigosdev-tooling`: 定义 BigOS 开发期 Python 工具 package 的稳定 CLI、模块边界、旧入口删除、镜像 patch 能力收敛和验证要求。

### Modified Capabilities
- `one-command-boot-debug`: 将稳定 Python helper 路径从 `tools/boot_debug.py` 迁移到 `python -m tools.bigosdev`，并保持现有 run target、镜像生成、emulator 启动和串口 marker 契约。
- `runtime-smoke-validation`: 将 runtime smoke matrix 入口迁移到 `tools.bigosdev smoke matrix`，保持 case、marker、日志、报告和失败语义不变。
- `x86-bootloader-hardening`: 将 `tools/install.py --with-boot` 等旧 installer 契约迁移为 `tools.bigosdev image patch`，保持只覆盖已有、连续、容量足够的 `/boot/boot.bin` 等安全边界。
- `source-layout-organization`: 将开发工具布局从独立脚本更新为 `tools/bigosdev/` package，并删除旧 `tools/install.py`/`tools/boot_debug.py` 活跃入口。
- `project-quality-assurance`: 更新推荐验证命令、Python 静态检查目标和 QEMU/Bochs helper 路径。

## Impact

- 受影响代码：`tools/boot_debug.py`、`tools/install.py`、新增 `tools/bigosdev/**`、`xmake/run_targets.lua`、Python 测试和 source-level tests。
- 受影响文档：`README.md`、`README-zh.md`、`AGENTS.md`、`docs/en/**`、`docs/zh/**` 中所有活跃 helper 路径、命令示例和工具说明。
- 受影响 OpenSpec：上述 modified capabilities 的基础 spec 和本 change 的 delta specs；归档 OpenSpec 历史记录可保留历史命令，但活跃规范和文档必须指向 `tools.bigosdev`。
- CLI/API 影响：旧 `uv run python tools/boot_debug.py ...` 和 `uv run python tools/install.py ...` 不再受支持；新入口为 `uv run python -m tools.bigosdev ...`。
- 架构假设：x86_64 Legacy BIOS/MBR/exFAT 兼容路径、UEFI ESP/root image 路径、QEMU/Bochs emulator 语义、磁盘布局、kernel handoff ABI、IDT/syscall vector、smoke marker 字符串和默认关闭 smoke 开关均不改变。
- 非目标：不新增 OS 运行时功能，不改变 bootloader、kernel、userland、文件系统或 storage driver 行为，不引入 CI 平台集成，不保留旧脚本 shim。
