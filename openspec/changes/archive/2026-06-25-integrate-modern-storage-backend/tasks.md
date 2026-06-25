## 1. 现代后端选择与请求层集成

- [x] 1.1 审查现代块存储后端发布路径、设备角色命名和现有块设备查找入口，确认显式选择不会改变默认 ATA/exFAT/userland baseline。
- [x] 1.2 实现或收紧现代后端的内核内部 selector，使未发布、probe 失败或未 ready 的后端返回确定性 not-found/not-ready 诊断。
- [x] 1.3 将现代后端接入块 I/O 请求层的普通提交路径，复用现有请求校验、队列绑定、issue、等待和 terminal status 归一化。
- [x] 1.4 加固现代后端请求的 device identity、queue slot、request pointer 和 generation 匹配，确保 timeout 或槽位复用后的迟到 completion 被拒绝或诊断。
- [x] 1.5 验证现代后端队列容量、失败和 timeout 不影响 ATA/RAM 等其他后端队列，并保留其他后端既有行为。

## 2. Cache 与写回路径集成

- [x] 2.1 让 page/buffer cache 能以现代后端作为设备 key 执行 cache load，失败时不得发布 invalid 数据块为有效内容。
- [x] 2.2 让现代后端 dirty block 的显式 sync、device-scoped sync 和 eviction writeback 全部通过请求层同步 wrapper 观察 terminal success。
- [x] 2.3 确保现代后端写回失败、queue full、issue failure、timeout、completion rejection 或 device error 时保留 dirty 或 pending 状态。
- [x] 2.4 保持 cache load/writeback 只在可阻塞内核上下文执行，确认现代后端 IRQ/completion 路径不触发 cache eviction、dirty scanning 或 filesystem policy。

## 3. 持久 clean-sync `/rw` 集成

- [x] 3.1 增加默认关闭或显式配置路径，使持久 clean-sync `/rw` 验证可以选择现代后端作为 backing block device。
- [x] 3.2 确保持久 `/rw` 在现代后端上只通过 VFS、cache 和块请求层执行 I/O，不调用现代驱动私有 filesystem hook。
- [x] 3.3 确保 `fsync`、显式 sync、eviction writeback 和 clean validation boundary 仅在所需 dirty data/metadata 块写回 terminal success 后报告成功。
- [x] 3.4 验证现代后端同步失败不会清除 dirty 状态、不会声明 durable success，并且不会破坏既有默认 `/rw` 策略。

## 4. 验证、诊断与文档边界

- [x] 4.1 增加默认关闭现代后端集成 smoke，覆盖显式选择、请求层读写、cache-mediated 写读往返、写回失败或 timeout 传播。
- [x] 4.2 增加现代后端持久 clean-sync 验证，覆盖同步后 clean reboot 或等价 remount 读回，并明确不声明 crash consistency 或 power-loss recovery。
- [x] 4.3 保留默认启动回归验证，确认未启用现代后端集成验证时 ATA/exFAT/userland baseline 不依赖现代后端。
- [x] 4.4 对 C++ 源码和头文件改动运行 `xmake`，并执行尽可能接近 freestanding x86_64 C++17 配置的 clang/clangd 辅助检查；若工具链或配置不可用，记录缺失项、历史诊断和残余风险。
- [x] 4.5 对运行时路径在可用环境下运行 QEMU headless smoke；若现代设备、MSI-X、磁盘镜像、QEMU/Bochs、x86_64-elf toolchain 或 serial capture 不可用，记录 skipped/blocked 覆盖项而不是声明通过。
- [x] 4.6 更新必要的项目文档或验证记录，使用仓库相对路径描述能力边界，避免声称默认存储替换、用户可见 async I/O、设备节点、完整 POSIX 存储栈或完整持久文件系统语义。
