## Why

现代块存储后端已经具备驱动与块层集成基础，但还缺少面向仿真器路径的可复现验证闭环。需要把现代存储设备的发布、请求完成、缓存/回写集成和默认启动回归放入受控验证流程，同时明确不引入新的指令集架构或默认启动依赖。

## What Changes

- 新增现代存储驱动的仿真器验证能力，覆盖设备发布、块请求读写、缓存/回写路径、失败/timeout 记录和默认启动回归。
- 将现代存储验证作为默认关闭的 runtime smoke 或等价验证用例接入现有 QEMU headless 串口 marker 与结构化 artifact 流程。
- 约束验证路径继续使用当前 x86_64 Legacy BIOS/MBR/exFAT 基线和现有工具链，不新增 ISA、不要求 UEFI parity、不替换默认 ATA 启动路径。
- 增强验证记录：区分工具/仿真器不可用、现代设备不可用、驱动发布失败、请求层失败、缓存/回写失败和默认启动回归失败。

## Capabilities

### New Capabilities
- `storage-driver-emulator-validation`: 定义现代存储驱动通过仿真器路径进行可复现验证的行为、边界、环境假设和结果记录。

### Modified Capabilities
- `runtime-smoke-validation`: 增加默认关闭的现代存储仿真器验证用例及其 artifact 记录要求。
- `modern-block-storage-driver`: 收紧现代存储后端验证合同，要求仿真器验证覆盖块层、缓存/回写和默认启动非依赖性。

## Impact

- 受影响子系统：存储驱动、设备框架、块请求层、page/buffer cache、writeback、runtime smoke 验证工具和仿真器启动配置。
- 架构假设：继续限定为当前 x86_64 内核路径；本 change 不新增 RISC-V、ARM 或其它 ISA 后端。
- 内存/布局假设：不得改变 kernel link address、BootInfo/handoff ABI、页表布局、IDT/syscall 向量、磁盘启动布局或现有 smoke marker ABI。
- 仿真器/工具链假设：优先使用 QEMU headless 路径进行自动化串口 marker 验证；Bochs 或交叉验证仅在设备模型与本地环境支持时记录；`x86_64-elf-*` 工具链、xmake、uv 与仿真器不可用时必须显式记录跳过和剩余风险。
- 非目标：不实现新 ISA、不把现代存储后端设为默认启动依赖、不新增用户可见设备 ABI、不实现 NVMe/virtio 之外的广泛存储矩阵、不要求 UEFI 存储/backend parity。
