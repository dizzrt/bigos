# 验证记录

## 执行结果

- `openspec status --change "promote-behavior-validation" --json`：通过，proposal、design、specs、tasks 均被识别为 `done`。
- `openspec validate "promote-behavior-validation" --type change --strict --json`：通过，1 个 change item valid，issues 为空。
- `command -v uv xmake x86_64-elf-gcc qemu-system-x86_64 bochs`：本机工具均可用。
- `uv run python tools/boot_debug.py runtime-smoke-matrix --case default-init --output build/test/promote-behavior-validation-runtime-smoke.md --serial-log-dir build/test/promote-behavior-validation-runtime-smoke --image-dir build/test/promote-behavior-validation-runtime-smoke --keep-going`：通过，`default-init` 观察到 `BIGOS_INIT_ENTER` 和 `BIGOS_USER_EXEC`。
- `GetDiagnostics` for `docs/en/arch/runtime-smoke-validation.md` and `docs/zh/arch/runtime-smoke-validation.md`：无诊断。

## 范围说明

- 本次实现只更新文档和 OpenSpec 任务记录：`docs/en/arch/runtime-smoke-validation.md`、`docs/zh/arch/runtime-smoke-validation.md`、`openspec/changes/promote-behavior-validation/tasks.md` 和本验证记录。
- 未修改 Python helper、C/C++/assembly 或 build 配置，因此不需要 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`、额外 `xmake` 构建或 clang/clangd 辅助诊断。
- `default-init` 行为 smoke 已经通过 xmake 构建 kernel、boot artifacts 和 user init ELF，并在 QEMU headless 下完成最窄运行时行为验证。
- Bochs/QEMU+Bochs 交叉验证未执行：本次没有修改 boot、IRQ、timer、ATA PIO、port IO、display/input 或其他硬件敏感源码；底层交叉验证保持场景化补充证据。

## 一致性记录

- 行为矩阵已覆盖默认 init 和 `/bin/sh`、简单 C 程序、`exec`/`wait`、fd 继承、`dup`、redirection、pipe、RAM-backed `/rw` 文件系统、失败信号、验证层和环境依赖。
- 文档明确缺失工具、emulator、串口日志、ROM/display、键盘输入或磁盘镜像配置时必须记录 `skipped` 或 `blocked`、替代检查和残留风险。
- `roadmap.md` Stage 26 保持项目规划级描述；未加入具体命令、marker、源代码入口、文件路径或 archive 索引。
- 行为矩阵不要求 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe、SMP、动态链接、完整 POSIX libc、作业控制或完整 shell 语义。
