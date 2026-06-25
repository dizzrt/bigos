## Context

当前 `tools/boot_debug.py` 已经承担多个相互独立的开发期职责：xmake 构建产物编排、Legacy raw image 创建与校验、UEFI ESP/root image 创建与校验、font asset 转换、persistent `/rw` 测试盘创建、Bochs 配置生成、QEMU/Bochs 启动、串口 marker 等待、runtime smoke matrix 执行和 Markdown 验证报告生成。该文件约 2600 行，测试也直接 import 大量内部函数，后续继续叠加功能会让修改风险集中到单个脚本。

`tools/install.py` 是更早的 boot artifact 安装器，当前日常 xmake run target 不再调用它；它的剩余有效价值是对已有 BIOS/MBR/exFAT 镜像执行受限 patch：覆盖 MBR、DBR、extended DBR，或在已有、连续、容量足够的 `/boot/boot.bin` 上覆盖写入 `boot.bin`。该能力仍应保留，但不应继续以独立泛名脚本存在。

本变更只重构 Python 开发工具层。受影响子系统是 developer tooling、boot image packaging、emulator launch 和 runtime smoke validation；不修改 kernel runtime、bootloader 二进制语义、磁盘布局、handoff ABI、IDT/syscall vector、page-table layout 或 smoke marker 字符串。

## Goals / Non-Goals

**Goals:**

- 建立 `tools/bigosdev/` 可执行 Python package，作为 BigOS 开发期工具集唯一稳定入口。
- 删除 `tools/boot_debug.py` 和 `tools/install.py`，不提供兼容 shim。
- 将旧 `boot_debug.py` 能力拆分到清晰模块：CLI、路径/常量、错误、进程与工具检查、构建产物、font asset、镜像、emulator、runtime smoke 和报告。
- 将旧 `install.py` 能力迁入 `tools.bigosdev image patch`，并保持受限 patch 语义。
- 更新 xmake run target、Python 测试、README、双语 docs、AGENTS、pyright 配置和基础 OpenSpec specs，移除活跃旧脚本引用。
- 使用 `uv run python -m tools.bigosdev ...` 作为文档化 Python helper 形式。

**Non-Goals:**

- 不保留 `tools/boot_debug.py` 或 `tools/install.py` shim。
- 不新增内核、bootloader、VFS、storage driver、UEFI runtime parity 或 CI 平台功能。
- 不改变 Legacy BIOS/MBR/exFAT raw image 布局、UEFI ESP/root image 语义、persistent `/rw` 测试盘语义、QEMU/Bochs 设备配置、serial log 默认目录或 runtime smoke marker。
- 不把 roadmap 变成命令或验证细节索引；具体命令仍放在专门文档、OpenSpec specs 和任务记录中。

## Decisions

### Package 入口

采用 package 入口而不是单文件脚本：

```text
tools/
  bigosdev/
    __init__.py
    __main__.py
    cli.py
    config.py
    errors.py
    process.py
    artifacts.py
    font_asset.py
    image/
      __init__.py
      exfat.py
      legacy.py
      uefi.py
      patch.py
      persistent.py
    emulator/
      __init__.py
      qemu.py
      bochs.py
    smoke/
      __init__.py
      cases.py
      runner.py
      report.py
```

稳定调用形式：

```bash
uv run python -m tools.bigosdev run ...
uv run python -m tools.bigosdev image validate ...
uv run python -m tools.bigosdev image patch ...
uv run python -m tools.bigosdev smoke matrix ...
```

`__main__.py` 仅导入 `cli.main()` 并返回退出码，避免入口模块承载业务逻辑。

### CLI 形态

`cli.py` 负责 argparse、子命令分发和 exit code 映射。子命令按能力而不是历史脚本命名：

- `run`: 构建或选择 artifacts，创建/校验镜像，按参数启动 QEMU/Bochs，按需等待串口 marker。
- `image create`: 只创建并校验 Legacy 或 UEFI 镜像，不启动 emulator。
- `image validate`: 离线校验 Legacy raw image 或 UEFI ESP/root image。
- `image patch`: 原 `install.py` 能力，支持 `--with-mbr`、`--with-dbr`、`--with-exdbr`、`--with-boot`，但入口归属于 `bigosdev`。
- `smoke matrix`: 原 runtime smoke matrix，配置 xmake smoke 开关，执行 QEMU headless marker checks，生成 Markdown artifact。

`xmake/run_targets.lua` 不解析 helper 细节，只调用 `python3 -m tools.bigosdev run ...` 并转发 `--` 后参数，避免 shell quoting 与 emulator 细节扩散。

### 模块边界

- `config.py`: 仓库根、build/log 路径、默认 image 路径、boot mode、emulator/display 枚举、boot artifact 路径和 size bound。
- `errors.py`: `StageError` 等阶段化错误。
- `process.py`: `run_command()`、工具可用性检查、日志路径约束、process group 终止。
- `artifacts.py`: xmake 构建调用、artifact 存在性和大小校验、`PreparedArtifacts`。
- `font_asset.py`: Unifont hex 解析、glyph lookup payload 构建和 font asset 写入。
- `image.exfat`: exFAT checksum、目录项生成、目录解析、文件查找、partition/boot region 读取等共享低层逻辑。
- `image.legacy`: Legacy MBR/exFAT image layout、创建、校验。
- `image.uefi`: ESP/FAT image 创建、mtools copy、OVMF vars 处理、UEFI image 校验。
- `image.patch`: 旧 `install.py` 的受限 patch 能力，复用 `image.exfat`，不使用全局可变路径。
- `image.persistent`: 独立 persistent `/rw` 测试盘创建。
- `emulator.qemu`: QEMU command 构造、UEFI/Legacy QEMU command、serial marker 等待。
- `emulator.bochs`: Bochs config render、lock 清理、launch、serial marker 等待。
- `smoke.cases`: `RuntimeSmokeCase`、`SMOKE_OPTIONS`、`RUNTIME_SMOKE_MATRIX`。
- `smoke.runner`: case 选择、xmake 配置、运行单 case、blocked/failed/pass 结果构造。
- `smoke.report`: Markdown artifact 格式化和写入。

### 旧入口删除

本变更明确不做兼容 shim。删除旧入口后：

- 活跃文档和 specs 不再使用 `tools/boot_debug.py` 或 `tools/install.py`。
- source-level tests 不再断言旧脚本中存在 case 字符串，而是导入 `tools.bigosdev.smoke.cases` 或检查新模块。
- pyright、ruff、pytest 目标迁移到 `tools/bigosdev/**`。
- 归档 OpenSpec 历史记录可保留历史命令事实；活跃 specs 和文档必须迁移。

### Data flow

默认 `run` 流程：

```text
CLI args
  -> config/path resolution
  -> process/tool preflight
  -> artifacts build or skip-build collection
  -> image create + validate
  -> optional persistent image
  -> emulator command/config
  -> optional serial marker wait
  -> stage-aware result
```

`image patch` 流程：

```text
CLI args
  -> input artifact size checks
  -> open existing image rb+
  -> parse MBR partition table
  -> parse exFAT boot region / directory when needed
  -> bounded overwrite only
  -> checksum refresh when boot-region changes
```

`smoke matrix` 流程：

```text
case selection
  -> tool availability record
  -> per-case xmake f switch set
  -> run args construction
  -> run helper path
  -> marker observation
  -> Markdown artifact
```

## Risks / Trade-offs

- **Breaking CLI**: 个人脚本或旧文档命令会失效。处理方式是主动更新项目内活跃调用、文档和 specs；不做 shim 是为了避免新工具继续背旧命名债。
- **测试迁移风险**: 现有测试直接 import `boot_debug.py` 内部函数，拆分后需要重新定位到模块级 API。处理方式是先按能力建立模块测试，再删除旧大文件测试依赖。
- **低层镜像 patch 风险**: `install.py` 使用全局状态和独立 exFAT 解析。迁移时必须保持“不创建、不扩展、不分配 cluster、不更新 allocation bitmap”的安全边界，并用 targeted tests 覆盖 MBR/DBR/exDBR/boot patch。
- **文档/规格残留风险**: 旧路径引用分散在 README、docs、AGENTS、OpenSpec specs 和 source-level tests。处理方式是使用 targeted `rg` 搜索 `tools/boot_debug.py`、`tools/install.py`、`boot_debug`、`install.py`、`--with-boot` 等关键字，并明确归档历史记录是否保留。
- **运行验证成本**: 这是 Python tooling 重构，不修改内核 runtime；Python 静态检查和单元测试是必需项。若环境具备 QEMU/OVMF/cross-toolchain，应运行至少一个 `run --display none --expect-serial-marker` 或 `smoke matrix --case default-*` 验证入口迁移；不可用时记录缺失工具和剩余风险。
