## Purpose

定义 BigOS 用户程序构建与打包能力：使用现有交叉工具链将用户 C 程序与 crt0、用户 libc 静态链接为 bounded ELF64 `ET_EXEC`，并把 shell 与验证二进制打包进既有引导磁盘镜像的确定路径。

## Requirements

### Requirement: 用户程序构建为静态 ELF64

BigOS 构建系统 SHALL 把每个用户 C 程序与 crt0、用户 libc 一起，用现有交叉工具链（`x86_64-elf-gcc`/`x86_64-elf-as`/`x86_64-elf-ld`）以 `-nostdlib -static` 链接为 `ET_EXEC` ELF64 可执行文件，且不依赖宿主 libc 或动态链接。每个产物 MUST 受 bounded 体积上限约束；超限时构建 MUST 确定性失败并报告该产物与体积。

#### Scenario: 用户程序静态链接为 ELF64

- **WHEN** 构建系统编译一个用户 C 程序
- **THEN** 构建 MUST 把该程序与 crt0 及用户 libc 以 `-nostdlib -static` 链接为 `ET_EXEC` ELF64
- **AND** 产物 MUST NOT 依赖宿主 libc 或动态链接器

#### Scenario: 超出体积上限时构建失败

- **WHEN** 某个用户程序产物超过配置的 bounded 体积上限
- **THEN** 构建 MUST 以确定性错误失败并报告该产物路径与体积

### Requirement: 用户程序打包进磁盘镜像

BigOS 构建系统 SHALL 把构建出的用户程序（至少包含 `/bin/sh` 与用于验证的若干测试二进制）打包进引导磁盘镜像的确定路径下，使内核 VFS 能按绝对路径打开并加载它们。打包 MUST NOT 改动既有 boot/MBR/分区/exFAT 发现契约与既有镜像布局，仅新增 `/bin/*` 等文件。

#### Scenario: shell 与测试二进制被打包

- **WHEN** 默认构建完成镜像打包
- **THEN** `/bin/sh` 与配置的测试二进制 MUST 出现在磁盘镜像的确定绝对路径下
- **AND** 内核 VFS MUST 能按这些绝对路径打开并经 ELF 装载路径加载它们

#### Scenario: 打包不破坏既有镜像契约

- **WHEN** 新增用户程序打包到镜像
- **THEN** 既有 boot/MBR/分区/exFAT 只读发现契约与镜像布局 MUST 保持不变
- **AND** 打包 MUST 仅新增文件而不改动既有内核/boot 产物的位置与 ABI
