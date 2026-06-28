## 1. Inventory And Boundary

- [x] 1.1 盘点 `user/bin` 现有工具、默认 `/bin` 打包清单、文档中声明的工具集合，标出已满足规格、需要修正、需要新增和不纳入本批的工具。
- [x] 1.2 落实本批文本工具边界：`grep` 只规格化字面子串匹配，记录未支持 option/pattern 的确定性失败策略。
- [x] 1.3 将网络诊断类工具排除出本批默认核心工具，并在文档和任务记录中说明其留给后续 socket/userland 网络体验变更。

## 2. Core Utility Behavior

- [x] 2.1 修正或补齐文件字节流工具行为，覆盖 stdin、命名文件、多输入、读写失败、stdout/stderr 和退出状态。
- [x] 2.2 修正或补齐文本过滤工具行为，覆盖固定缓冲处理、字节语义、unsupported option 失败、pipe 输入和命名文件输入。
- [x] 2.3 修正或补齐路径、目录、元数据工具行为，覆盖 cwd 相对路径、`/boot` 只读观察、`/rw` 目录枚举、metadata 输出和错误诊断。
- [x] 2.4 修正或补齐运行期文件修改工具行为，覆盖 create/write/copy/move/rename/remove/truncate/touch 类操作、只读目标失败和部分失败退出状态。
- [x] 2.5 修正或补齐时间、进程辅助和 BigOS 专用维护工具行为，确保其错误报告、退出状态和非目标说明符合 bounded utilities 规格。
- [x] 2.6 统一工具公共 helper、参数解析、stderr 诊断和退出码约定，避免引入宿主 libc、动态链接、线程、locale、宽字符或完整 POSIX 依赖。

## 3. Build And Packaging

- [x] 3.1 更新用户程序构建配置，使本批默认支持工具都生成 freestanding static user ELF。
- [x] 3.2 更新默认 `/bin` 镜像打包清单，确保源码、构建产物、打包路径和 shell PATH 可见工具集合一致。
- [x] 3.3 保持现有 user ELF loader size bound 和镜像容量检查；若工具超限，缩小实现或明确从默认清单移除。
- [x] 3.4 若修改 Python 打包辅助脚本，运行或记录 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 的结果；若未修改 Python 文件，记录不适用。

## 4. Shell Composition And Runtime Validation

- [x] 4.1 扩展 `userland_smoke` 或等价验证程序，直接执行代表性核心工具并校验 stdout、stderr、退出状态和失败路径。
- [x] 4.2 扩展 shell 非交互验证，覆盖 PATH 执行、输出重定向、输入重定向、单级 pipe、cwd 相对路径和 unsupported shell/tool form。
- [x] 4.3 增加 `/boot` 只读输入与 `/rw` 运行期文件树组合场景，覆盖 list/stat/copy/move/read/remove 后跨工具观察一致性。
- [x] 4.4 验证文本过滤组合场景，覆盖 stdin pipeline、命名文件、多输入部分失败和 unsupported pattern/option。

## 5. Documentation

- [x] 5.1 更新 `docs/en` 中用户态能力说明，列出默认 bounded core utilities 分类、可见路径、组合能力和非目标。
- [x] 5.2 同步更新 `docs/zh` 对应文档，保持目录结构和语义与 `docs/en` 一致。
- [x] 5.3 检查文档、工具 help/comment 和 OpenSpec 文本，确保不声称完整 POSIX、GNU coreutils、hosted libc、完整 shell、locale、Unicode、regex、排序、权限或持久文件系统兼容。

## 6. Validation And Review

- [x] 6.1 运行 `openspec status --change broaden-core-userland-utilities` 并确认 artifacts 状态正常。
- [x] 6.2 运行 OpenSpec 解析/校验命令，确认新增 capability spec 语法、requirement 和 scenario 可解析。
- [x] 6.3 运行 `xmake` 或最窄有用的用户程序构建验证，确认所有默认工具和 shell 构建通过。
- [x] 6.4 在环境可用时运行 QEMU headless userland smoke，观察 bounded utilities 组合验证结果；若 QEMU、cross toolchain、xmake、磁盘镜像或 timeout oracle 不可用，记录缺失条件、替代检查和剩余风险。
- [x] 6.5 如实现触及内核、boot、IRQ、内存、driver、linker 或 ABI 相关文件，补充对应初始化顺序、地址/ABI、失败行为和 emulator smoke 审查；若未触及，记录不适用。
