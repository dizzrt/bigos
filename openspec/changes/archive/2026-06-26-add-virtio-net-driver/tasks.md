## 1. 前置审计与接口边界

- [x] 1.1 审计现有 PCI、MSI-X、device framework、virtio-blk 和 MMIO 映射代码，确认可提取到 virtio common helper 的 transport 机制与必须留在设备驱动内的语义状态。
- [x] 1.2 在通用 `bigos::device` 中定义网络设备 class/role/accessor 与内核内部 `NetworkDevice` 接口，限制为 ready/link/MAC/MTU、bounded TX、bounded RX 获取/归还和诊断状态，不暴露用户态 ABI。
- [x] 1.3 提取小型 virtio common helper，覆盖 modern PCI capability、common cfg、status/feature、split queue 配置和 queue notify，并让 virtio-blk 继续通过回归验证。
- [x] 1.4 增加默认关闭构建开关与初始化入口，确保未启用验证时默认 boot/storage/filesystem/userland baseline 不依赖 virtio-net。

## 2. virtio-net 探测与设备发布

- [x] 2.1 实现 modern-only virtio-net PCI 探测，解析 required virtio capabilities、映射 MMIO BAR、拒绝 legacy/transitional transport。
- [x] 2.2 实现 feature 协商、设备状态转换、MAC/MTU 读取与确定性失败诊断；缺少 `VIRTIO_F_VERSION_1` 或必需 feature 时不得发布设备。
- [x] 2.3 将成功初始化的 virtio-net 设备发布到 `bigos::device` 网络设备边界，并覆盖 absent/unsupported/failed/not-ready 查询结果。

## 3. RX/TX 有界队列

- [x] 3.1 为 RX 和 TX 分别初始化一个 split virtqueue，使用显式、对齐、可追踪生命周期的内核内存和物理地址配置 common cfg queue 字段。
- [x] 3.2 实现 RX 固定包缓冲预投递、used 完成记录、malformed length 拒绝、consumer 归还后 repost 的槽位状态机。
- [x] 3.3 实现 TX frame 有界提交、descriptor 链构造、设备 notify、pending/terminal 状态转换、invalid/no-slot/not-ready/issue-failure/timeout/device-error 区分。
- [x] 3.4 为 RX/TX 槽位加入 generation 或等价机制，拒绝迟到、重复或不匹配完成，避免复用槽位被错误唤醒或重复回收。

## 4. MSI-X 完成与 IRQ 安全

- [x] 4.1 为 virtio-net RX/TX 队列配置 MSI-X 向量，复用现有中断分发 ABI 与 LAPIC EOI 边界，不发送 i8259 PIC EOI。
- [x] 4.2 实现 RX/TX completion handler，只处理 bounded used ring、槽位状态更新和内核内部等待者通知，不分配内存、不阻塞、不访问 VFS、不解析协议、不进入 syscall/socket 语义。
- [x] 4.3 审查中断安全、reentrancy、MMIO/descriptor 访问顺序、memory barrier、设备 status 失败路径和 visible diagnostics。

## 5. 验证与工具接入

- [x] 5.1 增加默认关闭 virtio-net runtime smoke，覆盖设备发布、RX/TX 队列初始化、至少一次 TX 完成、至少一次可控 RX frame 接收和至少一个确定性失败或 timeout。
- [x] 5.2 增加宿主侧最小 tap 配置/清理脚本，覆盖 tap 创建或准备、权限/平台检查、packet injection 前置条件记录和失败后的清理路径；Python helper 必须通过 `uv run ...` 执行。
- [x] 5.3 接入 QEMU modern virtio-net + MSI-X + tap 验证配置，调用或引用最小 tap 配置脚本，并保持生成日志在 `logs/`、镜像/配置在 `build/test/`。
- [x] 5.4 执行 `xmake` 或等价 x86_64-elf 交叉工具链构建；若 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake 或依赖不可用，记录 blocker 与残余风险。
- [x] 5.5 在环境可用时执行 QEMU headless virtio-net smoke，并通过串口/日志确认 pass/fail；tap 脚本前置条件或环境缺失时记录跳过项，不声明 runtime 成功。
- [x] 5.6 执行默认启动回归，确认未启用 virtio-net 验证时 boot、storage、filesystem、`/rw`、shell 和 userland baseline 不依赖 virtio-net。
- [x] 5.7 执行 virtio-blk 回归验证，确认 virtio common helper 提取未改变块设备发布、请求层、MSI-X 完成和默认存储路径语义。
- [x] 5.8 视环境执行 Bochs 默认启动或中断边界回归；若 Bochs 不支持相关 virtio-net backend，仅记录其覆盖默认启动/中断边界而非 virtio-net runtime。

## 6. 静态检查、文档与收尾

- [x] 6.1 对新增/修改的 C++ 源和头文件运行 clang 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI；记录历史诊断、当前变更诊断和误报。
- [x] 6.2 对新增/修改的 C++ 源和头文件运行 clangd 辅助诊断，修复当前变更新增的有效错误/警告；无法等价配置 GCC 交叉环境时记录差距。
- [x] 6.3 如果修改 Python helper，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若 `uv` 不可用，记录 blocker，不静默改用系统 Python。
- [x] 6.4 更新必要的能力文档；若编辑 `docs/en` 或 `docs/zh`，同步对应语言镜像，并保持文档不声称完整网络栈或 socket ABI。
- [x] 6.5 编写 `validation.md`，分开记录已通过检查、无法运行的检查及原因、历史诊断、当前变更新增诊断、工具链/仿真器缺口和残余风险。
- [x] 6.6 运行 `openspec validate add-virtio-net-driver --strict`，修复 proposal、design、specs、tasks 或 validation 发现的问题。
