## ADDED Requirements

### Requirement: 用户态 rename 工具

BigOS SHALL provide a small userland path tool or equivalent shell-consumable user program that invokes the bounded libc rename wrapper and makes `/rw` file rename behavior observable from the default shell and packaged `/bin` path. The tool MUST support absolute paths and cwd-relative paths through the existing libc/kernel path contract, MUST report deterministic errno-based failures, and MUST NOT imply complete POSIX `mv`, recursive moves, globbing, interactive prompting, cross-device moves, symlink behavior, dynamic linking, hosted libc, or complete POSIX shell behavior.

#### Scenario: shell 中重命名 cwd 相对文件

- **WHEN** 用户在 cwd 位于 `/rw/work` 的 shell 中运行 rename 工具，把 `a.txt` 改名为 `b.txt`
- **THEN** 工具 MUST 通过 libc rename wrapper 发起操作并在成功时退出为成功状态
- **AND** 后续目录列举、文件内容查看或元数据工具 MUST 能观察到目标名称，且源名称不再可见

#### Scenario: 工具报告只读和跨后端失败

- **WHEN** 用户尝试通过 rename 工具修改只读 boot asset、跨后端移动路径、重命名缺失路径或覆盖已存在目标
- **THEN** 工具 MUST 报告 errno-based 错误并返回非零状态
- **AND** shell MUST 保持在有界 read-parse-execute 循环中，后续命令仍可运行

#### Scenario: 工具范围不承诺完整 POSIX mv

- **WHEN** documentation、help text、specs 或 validation 描述 rename 工具
- **THEN** 它们 MUST 将该工具描述为 BigOS 有界 rename 消费路径
- **AND** MUST NOT 声称支持完整 POSIX `mv`、目录树搬移、目标替换、跨设备复制回退、交互确认、备份、glob 或 locale-aware 输出

### Requirement: rename 行为验证可观察

BigOS SHALL provide behavior-oriented validation for the constrained rename capability through userland-visible behavior. Validation MUST cover successful `/rw` rename, cwd-relative rename, source disappearance, target content preservation, already-open fd behavior where practical, and deterministic failures for read-only backend, missing source, existing target, unsupported object type and unavailable environment-dependent checks.

#### Scenario: 验证覆盖成功 rename

- **WHEN** rename validation runs in an environment with required cross-toolchain, image packaging, shell/userland and emulator support
- **THEN** it MUST observe at least one `/rw` file renamed through the userland path
- **AND** the result MUST be decidable through deterministic stdout/stderr, exit status, serial/log output or another low-level runtime signal

#### Scenario: 验证覆盖失败路径

- **WHEN** rename validation exercises unsupported or failing cases
- **THEN** it MUST observe deterministic nonzero failure for missing paths, read-only backend mutation attempts, target-exists behavior, unsupported object type or cross-backend requests where applicable
- **AND** failures MUST leave the shell and unrelated fd/VFS state usable for subsequent commands

#### Scenario: 环境不可用时记录跳过

- **WHEN** QEMU, Bochs, the `x86_64-elf-*` cross-toolchain, xmake, ROM/display dependencies, disk image configuration, serial oracle or timeout controls are unavailable
- **THEN** corresponding runtime validation MAY be skipped with an explicit note of the missing dependency
- **AND** validation notes MUST record substitute checks and remaining risk rather than claiming runtime validation passed
