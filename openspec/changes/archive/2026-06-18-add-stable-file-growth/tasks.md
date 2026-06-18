## 1. 现状审查与边界确认

- [x] 1.1 审查 `/rw` 常规文件 size、逻辑块映射、free-space metadata、fd offset、truncate/open flags 和 page/buffer cache dirty state 的现有提交顺序
- [x] 1.2 确认 RAM-backed 与 persistent clean-sync `/rw` 后端当前共享和分叉的文件增长路径，记录需要统一的块分配、释放和复用边界
- [x] 1.3 审查现有 syscall、libc wrapper、shell/path tools 和验证入口，确定是否复用既有 truncate/open 行为或仅追加最小新入口

## 2. 稳定块分配与文件增长

- [x] 2.1 为 `/rw` 常规文件扩展写实现容量预检、最大文件大小检查、权限/对象类型检查和失败前不推进 fd offset 的提交规则
- [x] 2.2 实现追加写、跨块写入和写入超过旧 EOF 的有界增长，确保旧 EOF 到写入起点之间读取为零
- [x] 2.3 实现新分配块的清零或完整覆盖规则，确保复用块不会向新文件暴露旧内容
- [x] 2.4 确保扩展写成功后 read、stat/fstat、dup/fork/exec 继承 fd、独立 reopen 和目录树状态观察一致
- [x] 2.5 覆盖容量耗尽、cache block 耗尽、内核分配失败、非法用户缓冲和块 IO 失败路径，确保不发布半成品 size、块映射或 dirty success

## 3. 截断与块释放复用

- [x] 3.1 实现常规文件收缩截断，保留目标范围内前缀内容，发布新 size 后使尾部块安全进入可复用集合
- [x] 3.2 实现常规文件扩展截断，确保新增范围读取为零且 metadata 报告提交后的有界大小
- [x] 3.3 实现块所有权转移检查，防止同一数据块同时属于 live inode 映射和 free-space set
- [x] 3.4 拒绝目录、只读 exFAT 路径、缺失路径和不支持对象类型的 truncate/growth 请求，并保持只读后端隔离
- [x] 3.5 覆盖 truncate 失败回滚，确保旧 size、旧块映射、metadata、dirty state 和 fd offset 保持可解释

## 4. persistent clean-sync 与缓存写回

- [x] 4.1 将 persistent `/rw` 的扩展写、截断、free-space metadata 和 inode metadata 接入 page/buffer cache 写回边界
- [x] 4.2 确保 `fsync`、显式同步或淘汰成功前不声明文件增长或截断 durable，写回失败保留 dirty 或 pending-write 状态
- [x] 4.3 验证 clean reboot 后同步的扩展文件内容、zero gap、截断 size 和 retained prefix 可读回
- [x] 4.4 确保未同步 dirty 增长或截断不扩大持久性承诺，并在文档或验证记录中保持 clean-sync 边界

## 5. 用户态接口与文档同步

- [x] 5.1 按最小 bounded libc 范围补齐必要的 truncate/ftruncate wrapper、错误映射或路径工具，不声明完整 POSIX 兼容
- [x] 5.2 扩展 shell 或小型用户程序验证用例，覆盖追加写、seek-past-EOF 写、收缩截断、扩展截断和块复用
- [x] 5.3 更新相关 docs/en 与 docs/zh 镜像文档，说明 bounded 文件增长、截断、zero gap、clean-sync 和非目标边界

## 6. 验证与质量检查

- [x] 6.1 增加或扩展源码级测试，覆盖提交顺序、容量边界、块所有权、防旧数据泄漏和失败不推进 fd offset
- [x] 6.2 运行针对文件系统相关测试的 `uv run pytest ...`；若 `uv` 或测试依赖不可用，记录 blocker、跳过原因和残余风险
- [x] 6.3 运行 `xmake` 或等价 x86_64-elf GCC 交叉构建，确认当前变更未破坏 freestanding kernel build
- [x] 6.4 对新增或修改的 C++ 源/头执行尽可能贴近 freestanding C++17/x86_64 交叉环境的 clang 和 clangd 辅助检查，区分历史诊断、当前变更诊断和工具配置误报
- [x] 6.5 运行 RAM-backed `/rw` default-off smoke，验证增长、截断、zero gap、块复用和失败回滚；记录 emulator/toolchain/ROM/display 可用性
- [x] 6.6 运行 persistent clean-sync 双阶段 smoke，验证同步后的增长和截断状态跨 clean reboot 可见；若持久测试盘或 emulator 不可用，记录跳过原因和残余风险
- [x] 6.7 运行 `openspec validate add-stable-file-growth --strict`，修正 proposal/design/spec/tasks 格式或契约问题
