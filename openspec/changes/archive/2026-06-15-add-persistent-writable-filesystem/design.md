## Context

BigOS 当前具备只读 exFAT boot assets、RAM-backed `/rw` 可写后端、VFS/fd I/O、块缓冲缓存、块设备读写抽象和默认关闭的文件系统 smoke。现有 `/rw` 语义只保证当前 boot session 内一致，不承诺跨重启持久化；runtime filesystem maturity 已要求运行期语义避免阻碍未来持久化，但尚未定义磁盘布局、挂载既有卷、恢复或重启周期验证。

本 change 覆盖 `kernel/core/fs`、块缓冲缓存、块设备写路径、Legacy BIOS raw image 的持久化承载选择和用户可见 `/rw` 行为。它不改变 boot handoff ABI、linker/page-table 地址、IDT/syscall vector、用户态 syscall ABI 或 x86_64-only 交付目标。

## Goals / Non-Goals

**Goals:**

- 在现有 `/rw` 可写文件系统语义上增加有界持久承载，使 `fsync` 或等价同步成功后的文件数据、目录项和 metadata 能在下一次启动后重新挂载并观察。
- 明确持久卷的识别、格式化、挂载既有卷、只读 boot assets 隔离、容量边界、失败降级和验证合同。
- 复用现有 VFS/fd、权限、metadata、目录枚举、rename、page/buffer cache 与块写能力，避免重新定义用户态 ABI。
- 保留 RAM-backed `/rw` 作为非持久 fallback 和现有 smoke 的兼容路径。
- 确保所有持久化 I/O 只在允许阻塞、分配和同步块 I/O 的普通进程/初始化上下文执行。

**Non-Goals:**

- 不实现 dynamic linking/shared libraries、完整 POSIX filesystem、完整 POSIX mount 工具链或完整 libc 扩展。
- 不实现 journaling、crash-consistent replay、fsck、写屏障、async I/O、广泛块调度或多设备存储栈。
- 不引入硬链接、符号链接、ACL/xattr、完整目录 rename、POSIX atomic replacement、broad file-backed `mmap` 或完整 `DIR*`/`struct dirent` 兼容。
- 不改变 MBR/exFAT boot discovery、kernel/user packaging、UEFI backend、SMP、多架构 backend、boot 地址、页表布局或中断/syscall ABI。

## Decisions

### Decision: 持久后端作为 `/rw` 的可选承载

持久化能力扩展现有 `/rw` 可写后端，而不是新增一个并行用户可见路径。内核在初始化阶段识别明确的持久卷；识别成功时挂载为 `/rw`，识别失败或配置为非持久模式时保留 RAM-backed `/rw`。

Alternatives considered:

- 新增 `/persist` 挂载点：降低兼容风险，但会让现有 shell、用户工具和 smoke 需要同时处理两个写路径，不利于 persistent clean-sync /rw storage 的用户可见目标。
- 直接替换 RAM-backed 后端：语义更简单，但会移除已有非持久 smoke/fallback，增加早期实现和调试风险。

### Decision: 持久卷使用构建工具生成的独立测试磁盘

持久区域使用构建工具生成的独立测试磁盘承载，并通过固定 magic、当前格式版本、块大小、容量边界和校验字段识别。启动盘继续承载 MBR、exFAT boot assets、kernel 文件和 packaged user binaries；持久磁盘只承载 persistent `/rw` 卷。未知 magic、旧版本、未来版本、容量不合法或校验失败时，内核必须拒绝挂载该持久卷并按策略降级，不得自动格式化。

Alternatives considered:

- 直接在现有 exFAT 分区内写隐藏文件：复用镜像空间较方便，但会破坏只读 boot assets 边界，并引入 exFAT 写语义。
- 自动改写分区表：更像真实系统，但对 bootability 和验证风险过高，超出 persistent clean-sync /rw storage 的有界目标。
- 在 raw image 中使用固定保留区：比独立磁盘更接近单盘布局，但更容易因偏移、容量或构建脚本错误影响 bootability。

### Decision: 格式化与挂载既有卷分离

初始化路径应区分“发现可挂载的既有持久卷”和“显式格式化新卷”。正常 boot 不得在识别失败时静默格式化，避免错误覆盖数据。persistent clean-sync /rw storage 提供最小用户态 mkfs 工具用于显式格式化独立持久测试磁盘；默认关闭 smoke 也可以调用同一受控格式化能力。格式化必须写入完整初始超级块、位图、根目录和 metadata 后才发布挂载。

Alternatives considered:

- 识别失败自动格式化：便于首次启动，但会把数据损坏误判为新盘，违背持久性和调试可解释性。
- 只支持全新格式化不支持挂载既有卷：实现较小，但不能满足跨重启持久化的核心用户可见目标。
- 仅由内核初始化配置触发格式化：实现更小，但开发者缺少明确的用户可见准备路径，也不利于 shell/userland 层验证。

### Decision: `fsync` 和缓存写回是持久化提交边界

文件写入仍先进入 page/buffer cache；成功的 `fsync`、显式同步或缓存淘汰回写把脏块写入持久承载。只有同步成功后的状态需要跨重启可见；未同步 dirty 数据的跨重启结果不作保证，但不能影响已经成功同步的旧状态。

Alternatives considered:

- 每次 `write` 立即同步：实现语义直接，但会降低交互性能并扩大 ATA PIO 同步写路径暴露面。
- 引入 journaling 或事务日志：能提升 crash consistency，但复杂度和恢复路径超出当前研究内核阶段。

### Decision: 失败路径以降级和确定性错误为主

持久后端初始化失败、卷不兼容、块写失败、缓存回写失败、容量耗尽或 metadata 提交失败时，调用方必须看到确定性负 errno 或文档化诊断路径。初始化失败不得 panic，且不得影响只读 exFAT boot assets；运行期同步失败必须保留 dirty 状态或保持可解释旧状态，不能静默报告成功。

Alternatives considered:

- 初始化失败 panic：便于暴露开发错误，但会让 boot assets 可读能力依赖持久卷健康度。
- 写失败后丢弃 dirty 块：实现简单，但破坏 `fsync` 和持久化可靠性合同。

### Decision: clean reboot smoke 由同一脚本连续启动同一持久磁盘镜像

持久化验证由辅助脚本控制两次 emulator 启动。第一次启动使用独立持久测试磁盘完成格式化或挂载、写入、`fsync` 和写入阶段 marker；脚本保留同一个持久磁盘镜像并启动第二次，第二次启动重新挂载 `/rw`、读回同步状态并发出验证阶段 marker。内核不需要为该 smoke 新增自重启硬件路径。

Alternatives considered:

- 单次内核 smoke 内部触发 reboot：能减少脚本状态管理，但会引入额外 reset/ACPI/键盘控制器路径，不属于持久 FS 核心目标。
- 人工两次运行：实现成本低，但不适合作为可复现验证合同。

### Decision: 持久卷只支持当前格式版本

persistent clean-sync /rw storage 的持久卷只支持当前磁盘格式版本。旧版本、未知版本和未来版本必须拒绝挂载并返回确定性诊断，不做自动迁移、只读旧版挂载或自动格式化。

Alternatives considered:

- 自动迁移旧版本：更友好，但需要失败回滚、旧 layout 解析和更复杂验证，接近格式升级框架。
- 只读挂载旧版本：降低数据丢失风险，但仍需要维护旧格式解析路径，超出当前有界目标。

## Risks / Trade-offs

- [Risk] 当前 ATA PIO/Legacy raw image 写路径同步且慢，可能放大交互延迟 → Mitigation: 保持 write-back cache，优先在 `fsync`、显式 sync、淘汰和 smoke 中触发落盘。
- [Risk] 未实现 journaling 时掉电/模拟器强杀可能留下不一致卷 → Mitigation: 规格仅承诺 clean sync + clean reboot 的持久性，不承诺 crash recovery，并在文档/验证中明确边界。
- [Risk] 磁盘布局选择错误可能破坏 boot assets → Mitigation: 持久区域必须显式隔离、带 magic/version/size 校验，验证必须覆盖只读 exFAT 仍可读。
- [Risk] 格式化路径误触发会覆盖数据 → Mitigation: 正常挂载失败不得自动格式化；格式化必须由受控配置或默认关闭验证路径显式触发。
- [Risk] C++ freestanding 静态检查与 x86_64-elf-gcc 环境不完全等价 → Mitigation: clang/clangd 仅作为辅助诊断，仍以 xmake + cross GCC build 和 emulator smoke 为主要验证。
- [Risk] 重启周期 smoke 依赖本地 QEMU/Bochs、ROM/display、磁盘镜像和 cross toolchain → Mitigation: 任务要求记录可用性、跳过原因、替代检查和残余风险。

## Migration Plan

- 第一步：在当前 RAM-backed `/rw` 语义旁边增加持久承载抽象和持久卷识别，不改变默认 boot 成功路径。
- 第二步：实现显式格式化新卷和挂载既有卷，确保失败时降级为 RAM-backed `/rw` 或不发布持久挂载，并保持 exFAT 只读路径可用。
- 第三步：把 VFS/fd、metadata、目录枚举、rename、权限、容量和 `fsync` 语义连接到持久后端，保持现有 syscall ABI 不变。
- 第四步：增加默认关闭的持久 FS smoke，覆盖格式化、写入、同步、clean reboot 后挂载读回、只读资产隔离和失败路径。
- 回滚策略：保留 RAM-backed `/rw` 作为默认可回退模式；如持久挂载验证失败，可禁用持久承载配置而不移除现有运行期可写能力。

## Open Questions

- 暂无。persistent clean-sync /rw storage 当前固定采用：构建工具生成独立持久测试磁盘、最小用户态 mkfs 工具、同一脚本连续两次启动同一持久磁盘镜像、仅支持当前持久卷格式版本。
