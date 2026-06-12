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

### Requirement: 基线小型 C 程序作为一等构建产物

BigOS 构建系统 SHALL 将 基线小型静态 C 用户程序作为一等构建产物处理。每个程序 MUST 使用现有用户 crt0 与最小 libc，链接为 bounded ELF64 `ET_EXEC`，并拥有稳定的镜像安装路径、确定性构建失败语义和文档化用途。

#### Scenario: 小型 C 程序被统一构建

- **WHEN** 默认或 简单 C 程序基线相关构建目标编译用户程序
- **THEN** 每个 基线小型 C 程序 MUST 通过同一类 freestanding 用户程序构建路径链接
- **AND** 产物 MUST 保持 `-nostdlib -static`、ELF64 `ET_EXEC` 和 bounded 体积约束

#### Scenario: 构建失败具有确定性

- **WHEN** 某个 基线小型 C 程序缺少输入源、链接失败或超过体积上限
- **THEN** 构建 MUST 以确定性错误失败并标明对应用户程序产物

### Requirement: 小型 C 程序集合覆盖基础兼容行为

BigOS SHALL 提供一组有界小型 C 用户程序，用于覆盖 简单 C 程序基线兼容基线的基础行为：参数解析、环境读取、stdout/stderr 输出、退出码、wrapper 失败路径和文件描述符 I/O。每个程序 SHOULD 保持单一目的，但整体集合 MUST 覆盖这些行为类别。

#### Scenario: 程序集合覆盖参数和环境

- **WHEN** 简单 C 程序基线行为验证运行小型 C 程序集合
- **THEN** 至少一个程序 MUST 证明 `argc`/`argv` handoff 可观察
- **AND** 至少一个程序 MUST 证明 `envp` 或只读环境访问可观察

#### Scenario: 程序集合覆盖 I/O 和退出

- **WHEN** 简单 C 程序基线行为验证运行小型 C 程序集合
- **THEN** 程序集合 MUST 覆盖 stdout/stderr 输出、退出码传播和至少一个失败 wrapper 的错误报告

#### Scenario: 打包路径保持有界

- **WHEN** 基线小型 C 程序被安装进 boot 镜像
- **THEN** 程序 MUST 位于有界、确定的绝对路径下
- **AND** 打包 MUST NOT 改动既有 boot/MBR/分区/exFAT 发现契约或内核/boot 产物位置
