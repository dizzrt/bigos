## 1. Baseline Review

- [x] 1.1 审视现有 `/bin/sh` 解析、PATH 查找、内建命令、外部命令、pipe 和 redirection 控制流，记录当前支持的 bounded grammar 与已知失败路径
- [x] 1.2 审视用户 libc wrapper、path tools、wait/exit、pipe/dup2、fd/VFS 和 cwd-relative path 的调用关系，确认不需要改变 syscall ABI 或 boot/image layout
- [x] 1.3 明确 bounded status 折叠规则，包括内建命令、命令缺失、exec 失败、redirection setup 失败、pipe setup 失败，以及 pipeline 成功启动后采用末端命令状态

## 2. Shell Error And Status Hardening

- [x] 2.1 为 shell 解析失败、unsupported syntax、路径过长、argv/pipe 上限超界和空命令边界补齐确定性错误与循环恢复
- [x] 2.2 为 PATH 查找失败、`fork` 失败、`execve` 失败和子进程非零退出补齐 bounded status 记录与可观察错误输出
- [x] 2.3 为内建命令和外部命令统一最近一次 shell-local bounded status 更新路径，并保持该状态不扩展为完整 POSIX `$?`、变量展开或 shell variable 语义；若需要交互观察，仅实现 BigOS-specific `status` 内建
- [x] 2.4 确认所有新增错误路径只使用 freestanding 用户态能力，不依赖 hosted libc、动态分配失控、异常、RTTI 或 OS 服务

## 3. FD Isolation And Composition

- [x] 3.1 将 redirection setup 调整为失败可恢复路径：open/dup2 失败时关闭中间 fd，恢复父 shell stdin/stdout/stderr，并避免执行目标命令
- [x] 3.2 将 single-pipe setup 调整为失败可恢复路径：pipe/fork/dup2 任一失败时关闭已创建端点，等待或清理已发布子进程，并保留父 shell fd 可用性
- [x] 3.3 确认成功 redirection 和 pipe 组合只影响目标子进程 fd 映射，不污染父 shell 无关 fd
- [x] 3.4 检查 wait/reap 与 fd close 生命周期，确保失败和成功组合都不会泄露 eligible child、open file reference 或 pipe endpoint

## 4. Userland Tool Composition

- [x] 4.1 确认 packaged path tools 在 PATH 查找和 cwd-relative path 下返回 deterministic stdout/stderr 与 errno-based 非零失败状态
- [x] 4.2 覆盖路径工具输出重定向到 writable runtime path 后可由后续 file-content 或 metadata 工具观察的组合路径
- [x] 4.3 覆盖路径工具或简单 C 程序通过 single pipe 组合时的数据传递、末端命令状态规则、上游失败可观察性和 shell 恢复路径
- [x] 4.4 确认帮助文本、文档或验证说明不暗示完整 POSIX utility suite、globbing、recursive traversal、symlink traversal 或完整 shell language

## 5. Validation

- [x] 5.1 运行最窄有用的 source/build 检查，优先使用 xmake 与 `x86_64-elf-*` 交叉工具链；若工具缺失，记录缺失项、替代检查和剩余风险
- [x] 5.2 对涉及 C/C++ 的改动运行可行的 clang/clangd 辅助诊断，按 freestanding C17/C++17、无 exceptions、无 RTTI 和 x86_64 目标配置；若不可用，记录原因和风险
- [x] 5.3 在环境可用时通过现有 `userland_smoke` 扩展运行 shell/userland 行为验证，覆盖成功命令、命令缺失或 unsupported syntax、cwd-relative path、output redirection、single pipe、bounded status 和失败后继续运行命令；不新增独立 shell usability smoke 开关
- [x] 5.4 若运行 emulator 验证，优先使用 QEMU headless/serial/log 路径；Bochs 或 QEMU+Bochs 仅在涉及低级硬件行为时作为可用性补充
- [x] 5.5 在验证记录中分离 passed、skipped、blocked、historical diagnostics、current-change diagnostics、替代检查和剩余风险，且不得把环境不可用的 runtime 检查记为通过

## 6. Documentation And Spec Sync

- [x] 6.1 更新相关 docs 时以 `docs/en` 为 canonical，并同步 `docs/zh` 对应路径；仅描述 bounded shell usability，不扩大 POSIX 承诺
- [x] 6.2 保持 `roadmap.md` 为 project-planning 粒度；若需要记录实现入口、命令、marker 或验证日志，放入 OpenSpec、docs 或 validation notes，而不是 roadmap
- [x] 6.3 在实现完成后运行 OpenSpec 状态或校验命令，确认 `harden-shell-usability` 的 proposal、design、specs 和 tasks 可用于 apply/archive 流程
