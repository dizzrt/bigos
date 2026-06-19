## Context

BigOS 当前已经有若干硬件后端：VGA 文本输出、i8259、PIT、CMOS RTC、ATA PIO 块设备，以及 RAM-backed/persistent `/rw` 用到的块设备对象。但这些设备还不是通过统一框架发布：VFS 在初始化时直接创建 ATA PIO primary master；bigfs 在持久模式下直接创建 persistent test ATA；PIT、VGA 和 CMOS RTC 也主要由对应子系统直接调用初始化或访问函数。

本 change 的目标是把“驱动如何注册、设备如何被 probe、内核子系统如何找到已探测设备”收束到一个 freestanding-safe 边界。它不是完整设备模型，也不是块层重写；现有 `BlockDevice` 同步读写 ABI、ATA PIO 后端、PIT/VGA/CMOS RTC 行为和文件系统行为应保持稳定，只把设备发现、初始化发布和消费入口集中管理，为后续块层、第二块设备后端和更多设备类别留下明确入口。

约束：
- 当前运行目标仍是单核 x86_64 Legacy BIOS 路径。
- 初始化顺序必须在端口 I/O 可用、基础内存管理可用之后执行；probe 不从 IRQ、scheduler critical section 或 preemption-disabled 路径执行阻塞 I/O。
- 不改变启动地址、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI 或现有 `BlockDevice` 读写函数语义。
- 框架必须 freestanding-safe，不能依赖 hosted libc、异常、RTTI、线程、动态链接或不受控全局构造顺序。

## Goals / Non-Goals

**Goals:**
- 建立有界设备表、驱动表和 probe/publish 状态机。
- 支持按设备类别、实例 id 或稳定角色查找已探测设备。
- 让现有 ATA PIO boot disk 与 persistent test disk 经框架注册和 probe 后发布给 VFS/bigfs。
- 让 PIT timer、VGA text device 和 CMOS RTC 也经框架注册/probe 并接入正常初始化路径，同时保留其现有硬件访问语义。
- 保持现有同步 `BlockDevice` API、错误状态、扇区大小、MBR/exFAT 发现和 persistent `/rw` clean-sync 语义。
- 对重复注册、容量耗尽、驱动不匹配、probe 失败和未探测设备查找提供确定性错误。
- 通过源码级检查和默认关闭 runtime smoke 验证框架行为，以及现有 boot asset 读取和 `/rw` 路径不回退。

**Non-Goals:**
- 不实现 async I/O、request queue、通用块层调度、DMA、interrupt-driven storage I/O 或后台 probe。
- 不新增第二块设备后端，不接入 virtio、AHCI/SATA、NVMe 或新 ISA。
- 不实现完整 bus/device tree、ACPI/PCI 枚举、热插拔、电源管理或用户态设备节点。
- 不启用 SMP，不引入跨 CPU probe、远端中断路由、IPI 或新的锁层。
- 不改变 UEFI backend spike 的运行时等价状态。

## Decisions

1. 设备框架采用静态有界 registry。

   使用固定容量的设备记录表和驱动记录表，显式返回 `NoSpace`、`Exists`、`NotFound`、`ProbeFailed` 等状态。这样符合当前 freestanding 内核和初始化阶段约束，也避免在 probe 期间引入不可控动态分配。备选方案是用可增长容器保存设备；该方案会把内存分配失败和初始化顺序复杂度扩散到早期驱动路径。

2. 设备和驱动用类别加稳定 id 匹配，而不是完整 bus model。

   框架先支持最小字段：设备类别（如 block、irqchip、timer、video、rtc）、实例 id、内部稳定角色/flags、driver 私有上下文、published 状态和指向 class-specific interface 的指针。ATA PIO 设备可用静态描述注册为 block 类设备，probe 成功后发布 `BlockDevice*`；PIT、VGA 和 CMOS RTC 也通过对应类别发布最小 class-specific interface。备选方案是一次性设计 PCI/ACPI/bus 树；这超出当前交付目标，也会提前绑定尚未实现的硬件枚举。

3. probe 是显式阶段，失败不发布设备。

   驱动注册和设备注册只记录描述符；`probe_all()` 或按类 probe 在内核初始化的普通上下文运行。probe 成功后设备状态从 registered 变为 probed/published；probe 失败保留错误状态，消费者查找不会拿到半初始化接口。备选方案是在注册时立即 probe；该方案让注册顺序更脆弱，也更难在框架初始化前后分离静态描述和实际 I/O。

4. VFS/bigfs 通过框架查找块设备，同时保留 `BlockDevice` 作为块 I/O 契约。

   `BlockDevice` 已经承载同步整扇区读写和错误状态，文件系统不需要知道设备框架内部结构。改造后 VFS 查找 boot disk 角色，bigfs 查找 persistent test disk 角色；若查找失败，返回现有确定性 VFS/bigfs 错误并保持 RAM-backed fallback 语义。备选方案是把文件系统直接迁移到新的通用设备对象；该方案会扩大调用面并增加不必要的行为变化。

5. persistent test disk 使用框架内部稳定角色，不新增用户可见 ABI。

   框架内部区分 boot disk 和 persistent writable disk，但这个区分只作为内核内部稳定角色存在，不暴露为用户态 syscall、文件系统可见设备编号或验证工具必须依赖的外部 ABI。验证应通过现有 smoke、serial marker 和文件系统行为证明 persistent 后端工作，而不是依赖具体设备编号。备选方案是把 persistent disk id 暴露给验证工具；该方案会过早固定枚举顺序和测试接口，增加后续替换后端或引入第二块设备时的兼容成本。

6. PIT、VGA 和 CMOS RTC 接入正常初始化路径，但不改变硬件语义。

   第一版不只迁移 block 类设备；PIT timer、VGA text device 和 CMOS RTC 也注册到 registry，并由正常初始化路径通过框架 probe/publish 后使用。为降低风险，class-specific interface 只封装当前已有初始化/访问入口，不重排 IRQ vector、不改变 PIT channel 配置、不改变 VGA/MMIO/port I/O 常量、不改变 RTC 读取边界。备选方案是这些设备只保留枚举和扩展位；该方案范围更小，但会让第一版框架只能验证块设备消费方，无法证明通用设备类别接入路径。

7. 中断和不可阻塞上下文只消费已发布设备，不执行阻塞 probe。

   当前 probe 可能执行端口 I/O 或块 I/O 轮询，因此必须限制在普通内核初始化或可阻塞进程上下文。IRQ handler、timer tick、syscall fast path 和 scheduler critical section 不得触发 probe 或注册需要阻塞的设备。未来 IRQ-safe 设备操作需要单独标注，不由本 change 隐式保证。

8. 初始化入口纳入正常驱动初始化顺序。

   设备框架初始化应在基础内存和端口 I/O 可用后、VFS/bigfs 依赖块设备前执行，并覆盖 PIT、VGA 和 CMOS RTC 的正常初始化入口。i8259/PIC 可先保留直接初始化边界，避免本 change 同时重排 interrupt controller ownership；但 timer/video/rtc consumer 应通过已发布设备或对应 wrapper 使用框架结果。备选方案是继续让 PIT/VGA/RTC 直接初始化；该方案会让 registry 无法覆盖用户要求的正常初始化路径。

## Risks / Trade-offs

- [Risk] 静态表容量不足会让后续设备扩展较快触顶。→ Mitigation: 初始容量按当前设备和近期扩展预留，容量耗尽返回确定性 `NoSpace`，后续块层或 bus 枚举变化可单独扩容。
- [Risk] 设备角色命名错误可能让 VFS/bigfs 绑定到错误块设备。→ Mitigation: 角色/id 使用内部枚举或固定常量，源码级测试覆盖 boot disk 与 persistent disk 查找，runtime smoke 验证 boot asset 和 `/rw` 路径；不把该 id 暴露为用户可见 ABI。
- [Risk] PIT/VGA/RTC 接入正常初始化路径可能触碰早期启动顺序。→ Mitigation: 只通过框架承载现有初始化/访问入口，不改变端口常量、IRQ vector、PIT channel、VGA 写路径或 RTC 读取语义；验证覆盖 bootability 和相关 smoke。
- [Risk] probe 失败后 fallback 行为不一致，可能隐藏真实硬件错误。→ Mitigation: boot disk 查找失败应让 VFS init 明确失败；persistent disk 失败可按现有契约回退 RAM-backed `/rw`，并记录状态。
- [Risk] 框架边界过早抽象会掩盖硬件细节。→ Mitigation: class-specific interface 仍保留显式 `BlockDevice`、端口 I/O 和驱动错误状态；框架只管理注册/查找/状态，不隐藏 ATA PIO 行为。
- [Risk] 未来 SMP 启用后 registry 需要锁保护。→ Mitigation: 当前实现标注单核初始化期边界，新增代码按既有 SMP preparation 约束记录未来锁和内存序要求，不声明多核安全。

## Migration Plan

1. 新增设备框架头文件和实现，定义状态、类别、描述符、驱动接口、注册、probe 和查找 API。
2. 将 ATA PIO boot disk 与 persistent test disk 描述符接入框架，probe 成功后发布现有 `BlockDevice`，并使用内部稳定角色区分 boot disk 与 persistent writable disk。
3. 将 PIT timer、VGA text device 和 CMOS RTC 接入 registry/probe/publish，并把正常初始化路径改为使用框架发布结果或 class-specific wrapper。
4. 调整 VFS/bigfs 初始化路径，通过框架获取对应块设备；保留现有直接初始化行为的最小 fallback 仅用于迁移期间验证，最终消费者不应直接构造硬件后端。
5. 补充源码级测试，检查框架 API、重复注册、容量边界、probe 失败、未发布查找失败、VFS/bigfs 不再直接初始化 ATA，以及 timer/video/rtc 正常初始化路径经框架发布。
6. 补充默认关闭 runtime smoke 或复用现有 filesystem/persistent/timer/RTC/console smoke，验证 boot asset 读取、`/rw` 后端、PIT tick、VGA/serial 可见输出和 RTC 读取行为不回退。
7. 若实现导致启动或文件系统 smoke 阻塞，回退设备框架接入点即可恢复到当前直接驱动初始化基线；不需要磁盘格式或用户态迁移。
