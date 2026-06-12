## 1. 基线核查

- [x] 1.1 核查现有 `user` crt0 是否按当前用户栈布局稳定传递 `argc`、`argv`、`envp` 给 `main`，并确认 `main` 返回后经退出 syscall 结束进程。
- [x] 1.2 核查现有用户 libc wrapper 的成功返回、失败返回、`errno` 设置、stdout/stderr 输出 helper 和最小环境访问行为，记录与简单 C 程序基线契约不一致的缺口。
- [x] 1.3 核查现有用户程序构建/镜像打包路径是否已能稳定构建多个 freestanding C 程序，并保留 `-nostdlib -static`、ELF64 `ET_EXEC` 和 bounded 体积约束。
- [x] 1.4 明确 简单 C 程序基线不新增 kernel syscall、不修改 `int 0x80` ABI、不改变 boot/磁盘/页表/链接地址布局；若实施发现必须改动，先回到设计/spec 更新。

## 2. 小型 C 程序集合

- [x] 2.1 整理或新增参数探针程序，输出可验证的 `argc`/`argv` 内容并以确定性退出码结束。
- [x] 2.2 整理或新增环境探针程序，输出可验证的 `envp`/只读环境访问结果；若环境边界为空，必须确定性报告该边界。
- [x] 2.3 整理或新增 stdout/stderr 程序，分别覆盖普通输出和错误输出路径。
- [x] 2.4 整理或新增失败 wrapper 探针程序，触发一个确定性失败 syscall wrapper，并验证返回值、`errno` 和错误报告。
- [x] 2.5 整理或新增退出码探针程序，使 shell 或验证路径能观察正常退出与非零退出状态。

## 3. 构建与打包

- [x] 3.1 将 基线小型 C 程序接入现有用户程序构建规则，确保每个程序与用户 crt0/libc 静态链接为 bounded ELF64 `ET_EXEC`。
- [x] 3.2 为每个 基线程序指定稳定镜像安装路径，并确保打包只新增有界 `/bin/*` 或同等用户程序文件，不改变 boot/MBR/分区/exFAT 发现契约。
- [x] 3.3 保留或调整有界体积上限；若需要调整，记录新上限原因，并确保超限构建确定性失败且报告产物与体积。
- [x] 3.4 运行最窄可用构建检查，优先使用 `xmake` 或相关用户程序 target；若 `x86_64-elf-*` 工具链不可用，记录阻塞原因与残留风险。

## 4. Shell 集成

- [x] 4.1 验证 `/bin/sh` 能向 基线小型 C 程序传递参数，并使程序观察到对应 `argc`/`argv`。
- [x] 4.2 验证 shell 能展示外部 C 程序的 stdout/stderr 输出，且不会把外部程序错误报告误判为 shell 崩溃。
- [x] 4.3 验证外部 C 程序正常退出或非零退出后，shell 回到有界读-解析-执行循环。
- [x] 4.4 若现有 shell 无法稳定保留退出状态可观察性，补齐最小验证路径，但不引入完整 POSIX shell、作业控制或复杂脚本语义。

## 5. 行为验证

- [x] 5.1 增加或扩展 simple C program baseline runtime behavior smoke，覆盖参数、环境、stdout/stderr、`errno` 翻译、退出码和 shell 执行小型 C 程序。
- [x] 5.2 保持 simple C program baseline emulator-dependent smoke 默认关闭或仅在显式验证模式中启用，不让默认构建强制依赖 QEMU/Bochs。
- [x] 5.3 在可用环境下运行 QEMU headless serial/log 验证；如需 Python helper，使用 `uv run python ...`，并记录通过结果、失败原因或跳过原因。
- [x] 5.4 如修改 Python 测试或 helper，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，明确记录阻塞。
- [x] 5.5 对无法自动化的图形控制台或 Bochs 交叉验证，记录手工验证步骤、未执行原因和残留风险。

## 6. 文档与收口

- [x] 6.1 更新简单 C 程序基线相关用户态文档，说明简单静态 C 程序基线、入口契约、wrapper/`errno` 约定、小型程序集合和非目标。
- [x] 6.2 如更新 `docs/en`，同步更新 `docs/zh` 对应相对路径，保持中英文技术事实一致。
- [x] 6.3 更新 `roadmap.md` 或相邻规划说明时保持路线图层级，只描述 简单 C 程序基线的项目级能力、边界和状态，不加入具体命令、marker、源码入口或版本索引。
- [x] 6.4 汇总验证记录，区分已通过检查、因工具链/模拟器/显示环境缺失而跳过的检查、替代检查和残留风险。
