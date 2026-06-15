## ADDED Requirements

### Requirement: Stage 41 文件系统行为验证
BigOS SHALL provide a dedicated default-off behavior-oriented runtime smoke for Stage 41 runtime filesystem maturity. Validation MUST cover current-runtime success and failure paths across read-only exFAT, RAM-backed `/rw`, fd/VFS operations, metadata queries, directory enumeration, cwd-relative paths, libc errno wrappers, and shell-visible user tools. Environment-dependent emulator checks MUST record missing QEMU, Bochs, cross toolchain, ROM/display, disk image, serial oracle, or timeout dependencies as skipped rather than passed.

#### Scenario: 运行期成功组合路径验证
- **WHEN** Stage 41 filesystem validation runs in an environment with the required build, toolchain, disk image, emulator, and serial observation support
- **THEN** it MUST exercise at least one read-only exFAT read/metadata path and one `/rw` create/write/read/lseek/fsync/stat/list/unlink/restricted-rename path
- **AND** it MUST verify that observed file contents, metadata, stable directory entry order, fd references, and shell/libc-visible results match the bounded filesystem contract

#### Scenario: 运行期失败组合路径验证
- **WHEN** Stage 41 filesystem validation runs
- **THEN** it MUST exercise representative failures for read-only write, missing path, existing target, invalid fd, invalid user buffer, permission denial, naturally filled `/rw` capacity exhaustion, unsupported object type, and directory enumeration output exhaustion
- **AND** it MUST verify deterministic errno behavior and state preservation for each failure class

#### Scenario: 环境不可用时记录跳过
- **WHEN** required emulator, cross toolchain, boot image, display/ROM, serial oracle, timeout, or local configuration dependencies are unavailable
- **THEN** validation notes MUST record the unavailable dependency, skipped cases, substitute checks, and residual risk
- **AND** they MUST NOT claim Stage 41 runtime filesystem validation passed

### Requirement: 验证保持 roadmap 边界
BigOS SHALL keep Stage 41 validation aligned with the bounded userland and non-persistent `/rw` roadmap boundary. Validation MAY use source-level checks, small static C programs, shell tools, and the dedicated default-off filesystem maturity runtime smoke, but MUST NOT require dynamic linking, complete POSIX test suites, SMP, UEFI runtime parity, broad storage drivers, or cross-reboot persistence checks.

#### Scenario: 专用 smoke 不替代基础回归
- **WHEN** Stage 41 filesystem maturity validation is added
- **THEN** BigOS MUST keep it as a dedicated default-off runtime path for cross-layer filesystem behavior
- **AND** existing writable filesystem and userland smokes MAY remain as narrower regression checks rather than carrying the full Stage 41 contract

#### Scenario: 验证不要求跨重启持久化
- **WHEN** Stage 41 validation writes files under `/rw`
- **THEN** validation MUST treat those files as current-session state only
- **AND** it MUST NOT require reboot-and-remount persistence unless a later accepted persistent-storage change adds that requirement

#### Scenario: 验证不扩大 POSIX 声明
- **WHEN** validation uses libc wrappers, shell commands, or small user tools to observe filesystem behavior
- **THEN** it MUST describe the checked behavior as a BigOS bounded filesystem subset
- **AND** it MUST NOT claim complete POSIX filesystem, shell, libc, directory stream, or metadata compatibility
