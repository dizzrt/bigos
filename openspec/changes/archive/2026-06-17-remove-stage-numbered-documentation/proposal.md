## Why

BigOS 已进入以功能完善为主的新阶段，`roadmap.md` 不再保留历史 stage 编号与具体改造项的对应关系。当前顶层文档、架构文档和 OpenSpec 规格中仍散落 旧路线图编号等历史表述，容易让读者误以为后续规划仍以 stage 索引作为事实来源。

## What Changes

- 清理顶层文档、`docs/en`、`docs/zh` 和 active OpenSpec specs 中的历史 stage/task 编号叙述，改为直接描述功能、能力、实现边界或验证矩阵。
- 将“后续新增 change、归档 change 和相关文档更新不得包含 roadmap stage/task 编号”纳入项目级要求，避免 OpenSpec artifact 继续把 roadmap 编号当作变更标识或事实来源。
- 保留非路线图语义的 `stage` 用法，例如 boot-stage 地址布局、first-stage boot code、验证流程中的 failed stage 字段，以及既有串口 marker 中的 `stage=<stage>` 输出契约。
- 将 旧路线图编号对应的用户态、libc、文件系统、runtime smoke 表述改为 bounded POSIX-like surface、bounded libc foundation、runtime filesystem maturity、persistent clean-sync `/rw` 等能力名。
- 清理 `openspec/changes/archive/**` 中历史 change 记录里的 roadmap stage/task 编号表述，改为能力、模块、实现边界或验证语义；archive 继续承担历史记录角色，但不继续保存旧 stage 编号索引。
- 同步中英文 docs 镜像，保持 `docs/en` 为 canonical、`docs/zh` 为对应中文镜像。
- 不修改内核源码、用户态源码、构建开关、验证 marker、boot layout、ABI 或 roadmap 的里程碑优先级。

## Capabilities

### New Capabilities
- `documentation-stage-reference-discipline`: 定义 BigOS 文档、active OpenSpec 规格和 archive 历史记录不得依赖历史 stage/task 编号描述当前能力、规划、验证边界或归档事实，而应使用功能/实现名称表达。

### Modified Capabilities
- `project-quality-assurance`: 增加文档一致性检查要求，确保 stage/task 编号清理后可通过 targeted search 验证，并要求后续新增、归档 change 与文档更新不得引用 roadmap 编号。

## Impact

- 影响范围：`README.md`、`README-zh.md`、`roadmap.md`、`AGENTS.md`、`docs/README.md`、`docs/AGENTS.md`、`docs/en/**`、`docs/zh/**`、`openspec/specs/**`、`openspec/changes/archive/**`。
- 不影响范围：`kernel/**`、`user/**`、`cpp/**`、`include/**`、`tools/**`、`xmake.lua`、boot artifacts 和 emulator helper 行为。
- 架构假设：默认可运行 backend 仍是 x86_64 Legacy BIOS/MBR/exFAT；UEFI 仍是非等价 backend spike；SMP、完整 POSIX、动态链接、广泛 file-backed `mmap`、完整持久文件系统和 async I/O 仍不在本变更范围内。
- 内存/磁盘/工具链假设：本变更不改变内存布局、页表布局、磁盘布局、emulator 参数或 `x86_64-elf-*` toolchain 要求。
