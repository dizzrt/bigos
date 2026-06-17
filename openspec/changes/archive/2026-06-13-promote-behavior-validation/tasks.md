## 1. 行为验证矩阵

- [x] 1.1 盘点现有 userland、shell、简单 C 程序、进程/fd、pipe/redirection 和 `/rw` 文件系统验证入口，区分默认路径、default-off smoke 和手工/场景化检查。
- [x] 1.2 定义行为导向验证矩阵，记录每个检查的 capability、输入、预期输出或状态、失败信号、验证层和环境依赖。
- [x] 1.3 确认矩阵不要求 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe、SMP、动态链接、完整 POSIX libc、作业控制或完整 shell 语义。

## 2. 运行时行为断言

- [x] 2.1 为默认 init 和 `/bin/sh` 路径补充可由 QEMU headless 串口/日志或等价确定性输出判定的行为断言。
- [x] 2.2 为简单 C 程序补充参数、环境处理、stdout/stderr、退出状态、`errno` 或 shell continuation 的代表性行为断言。
- [x] 2.3 为进程/fd 组合路径补充 exec/wait、fd 继承、dup、redirection、pipe 和用户可见错误路径的代表性行为断言。
- [x] 2.4 为运行时文件系统补充创建、读写、seek、sync、目录或删除效果的代表性行为断言，并保持在 RAM-backed `/rw` 和有界 VFS 能力内。
- [x] 2.5 确保失败路径明确发射失败结果或返回非成功状态，不能因缺少期望输出而静默通过。

## 3. 分层环境记录

- [x] 3.1 在验证记录中区分源码/spec 一致性、构建/打包检查、QEMU headless runtime 行为断言、Bochs/图形/人工输入补充证据。
- [x] 3.2 当 `x86_64-elf-gcc`、xmake、QEMU、Bochs、显示/ROM、串口日志、键盘输入或磁盘镜像配置不可用时，记录 skipped/blocked、替代检查和残留风险。
- [x] 3.3 对 boot、IRQ、timer、ATA PIO、port IO、display/input 等硬件敏感场景，优先记录 Bochs 或 QEMU+Bochs 交叉验证证据；不可用时记录风险。

## 4. 文档同步

- [x] 4.1 更新 docs/en 中的验证说明，描述行为导向验证矩阵、分层环境策略和 bounded userland/POSIX-like 边界。
- [x] 4.2 同步更新 docs/zh 对应 Markdown 路径，保持与 docs/en 结构一致且语义一致。
- [x] 4.3 复核 `roadmap.md` 中 TTY console input capability6 仍保持项目规划级描述，不加入具体命令、marker、源代码入口、文件路径或 archive 索引。

## 5. 验证

- [x] 5.1 运行 OpenSpec 状态或校验命令，确认 proposal、design、specs 和 tasks 均可被识别。
- [x] 5.2 运行最窄有用的构建或静态检查；若只改文档和 OpenSpec artifacts，则记录无需 runtime build 的原因。
- [x] 5.3 如实现修改 Python helper，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用则记录 blocker。
- [x] 5.4 如实现修改 C/C++/assembly 或 build 配置，运行 xmake/cross-toolchain 相关检查，并记录 clang/clangd 辅助诊断是否适用。
- [x] 5.5 如环境支持，运行 QEMU headless 行为 smoke 并记录通过项；如环境不支持，记录缺失工具、替代检查和残留风险。
