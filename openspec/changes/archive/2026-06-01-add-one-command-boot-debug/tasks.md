## 1. Baseline and Interface

- [x] 1.1 盘点现有启动相关入口、构建产物路径、`tools/install.py` 能力边界、参考 `bochsrc.bxrc`、Bochs 配置假设和当前缺失的测试资产。
- [x] 1.2 确定第一阶段对外命令名称和参数，例如主入口 `python3 tools/boot_debug.py run`，并提供项目级包装入口 `make boot-debug`。
- [x] 1.3 定义生成物路径，包括 raw image、Bochs 配置、临时目录和日志路径，确保默认位于 `build/` 下。
- [x] 1.4 记录非目标：不新增 QEMU、CI 自动判定、串口协议、内核运行时代码变更或完整 exFAT 文件系统实现。

## 2. Boot Debug Script Orchestration

- [x] 2.1 新增启动调试 Python 脚本，提供 `run` 子命令和必要参数，如 `--image`、`--image-size`、`--keep-image`、`--bochsrc` 或等价选项。
- [x] 2.2 实现 preflight 检查，覆盖 `python3`、`xmake`、`bochs`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld` 和 `x86_64-elf-as`。
- [x] 2.3 实现阶段化命令执行与错误报告，确保 kernel build、boot build、image build、Bochs launch 的失败能明确定位。
- [x] 2.4 编排 `xmake` 构建内核 ELF，并在构建失败时停止，不使用 stale kernel 产物继续启动。
- [x] 2.5 编排 `make -C src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot` 构建 boot 产物，并验证产物存在和尺寸约束。

## 3. User-space Raw Image Builder

- [x] 3.1 实现固定 raw disk image 创建逻辑，支持默认大小和显式 `--image-size`，不依赖 `diskutil`、loop device、挂载或外部 mkfs 工具。
- [x] 3.2 定义并集中维护磁盘布局常量，包括 MBR LBA、exFAT 分区起始 LBA、main/backup boot region、FAT、bitmap、root directory、`/boot` directory 和文件数据区。
- [x] 3.3 生成 MBR 分区表，写入 active exFAT partition entry，同时保留现有 MBR boot code 写入能力。
- [x] 3.4 生成最小 exFAT main boot region、backup boot region 和 checksum sector，使现有 bootloader 和 installer 逻辑能识别分区。
- [x] 3.5 生成根目录和 `/boot` 目录项，写入连续的 `/boot/boot.bin` 和根目录 `kernel` 文件，并正确填写 first cluster、data length 和 contiguous 标志。
- [x] 3.6 增加镜像布局自检，复用或对齐 `tools/install.py` 的 exFAT 解析逻辑，确认 `/boot/boot.bin` 和 `kernel` 可被定位且连续。

## 4. Installer Integration and Compatibility

- [x] 4.1 评估 `tools/install.py` 是否需要提取可复用函数；如需要，保持现有 CLI 参数兼容。
- [x] 4.2 将生成的 MBR、DBR、extended DBR 和 `boot.bin` 写入 raw image，保留现有 boot 地址和磁盘布局语义。
- [x] 4.3 校验 `tools/install.py --with-mbr`、`--with-dbr`、`--with-exdbr` 和 `--with-boot` 对新生成 raw image 仍可工作或明确由新脚本等价覆盖。
- [x] 4.4 确认未修改 `boot.s`、`boot.cc`、`BootInfo`、`link.lds`、内核链接地址、ELF 文件名 `kernel` 和 boot handoff 约定。

## 5. Bochs Launch Path

- [x] 5.1 从参考 `bochsrc.bxrc` 提取可移植硬件意图，包括 `boot: disk`、ATA flat disk、32 MiB memory、单 CPU、x86_64 CPUID 和 `log: -`。
- [x] 5.2 实现 Bochs 配置生成或模板渲染，使配置指向生成的 raw disk image，并避免硬编码 `win32`、Windows ROM 路径或 Windows image 路径。
- [x] 5.3 支持用户传入自定义 Bochs 配置、ROM 路径或配置补充项，以处理 host-specific BIOS/VGA BIOS 路径。
- [x] 5.4 实现 Bochs 启动命令，失败时保留 raw image 和配置路径供开发者排查。
- [x] 5.5 在顶层 `Makefile` 或 `xmake.lua` 增加薄包装入口，不复制 Python 脚本内部流程。

## 6. Documentation

- [x] 6.1 更新 `README.md` 和 `README-zh.md`，说明一行启动调试命令、依赖、生成物路径和第一阶段仅支持 Bochs。
- [x] 6.2 文档说明 raw image 由脚本完全用户态生成，不需要 macOS `diskutil`、挂载权限或手工 exFAT 镜像。
- [x] 6.3 文档说明常见失败原因：缺交叉工具链、缺 Bochs、内核构建失败、Bochs host-specific 配置无法推断，并说明历史 `bochsrc.bxrc` 仅作为参考不能直接跨平台复用。
- [x] 6.4 文档保留后续方向：QEMU/headless、串口自动判定和 CI smoke test 不属于本阶段。

## 7. Validation

- [x] 7.1 运行 `make -C src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot`，记录 boot 构建和尺寸检查结果。
- [x] 7.2 运行 `xmake`，记录内核构建结果；若存在历史编译阻塞，明确标注为非本变更新增并说明启动脚本会在该阶段失败。
- [x] 7.3 运行 raw image builder 的离线 layout validation，验证 MBR、exFAT boot region、backup region、`/boot/boot.bin` 和 `kernel` 均可解析。
- [x] 7.4 在 Bochs 可用时运行一行命令 smoke test；如 Bochs 不可用，记录缺失依赖和剩余 bootability 风险。
- [x] 7.5 对 Python 改动运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；如果工具不可用，记录原因和剩余风险。
- [x] 7.6 确认本变更未引入 C++ runtime、boot assembly、linker script、IRQ、内存管理或驱动源码改动；如实际实现触及这些区域，补充对应窄范围静态检查和 bootability review。
- [x] 7.7 更新验证记录，区分已通过检查、不可运行检查、历史诊断、当前变更新增问题和工具链/环境限制。

## Validation Notes

- `make -C src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot`：通过；生成 `mbr.bin` 512 bytes、`dbr.bin` 512 bytes、`exdbr.bin` 890 bytes、`boot.bin` 8712 bytes；保留既有 assembler `movsd` warning。
- `xmake`：失败于既有 `src/kernel/irq/isr.cc` 编译错误（`irq_handler`、`MAX_IRQ_NUM`、`isr_list` 未解析）；本变更未修改该文件，启动脚本会在 `kernel build` 阶段停止且不会继续使用 stale kernel。
- `python3 tools/boot_debug.py run --no-launch`：按预期失败于 `kernel build` 阶段，验证阶段化错误报告和停止行为。
- raw image 离线 layout validation：通过；使用新 builder、当前 boot 产物和 dummy kernel 生成 `build/test/layout-validation.raw`，随后 `python3 tools/boot_debug.py validate-image --image build/test/layout-validation.raw` 通过，覆盖 MBR、exFAT main/backup boot region、`/boot/boot.bin` 和根目录 `kernel` 解析。
- Bochs：`/opt/homebrew/bin/bochs` 可用；`make boot-debug` 已成功启动 Bochs 3.0，完成配置解析、CPU model 初始化、磁盘几何校验、设备初始化并进入 BIOS 阶段；本次 smoke test 由开发者主动停止，非配置错误退出。
- Python tooling：`uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 均通过；pytest 结果为 `4 passed`。
- 运行时代码边界：未修改 C++ runtime、boot assembly、`boot.cc`、`BootInfo`、`link.lds`、IRQ、内存管理或驱动源码；改动限定为 Python 工具、Makefile、README、pyright 配置和测试。
