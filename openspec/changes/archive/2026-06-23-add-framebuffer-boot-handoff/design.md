## Context

当前 UEFI backend 已能通过 BootInfo v2 把 core、memory map、storage metadata 和 loader metadata 传给内核，默认启动目标仍以 bounded userland baseline 为验证边界。后续图形控制台需要一个比 VGA text 更早、更稳定的输入契约：UEFI loader 在 `ExitBootServices` 前通过 GOP 获取线性 framebuffer，内核在早期初始化阶段只解析几何与像素格式，不直接调用 UEFI 服务。

本 change 跨越 x86_64 UEFI loader、公共 boot handoff header、内核早期 metadata consumer、内存保留规则和验证记录。它不改变 kernel link address、ELF64 加载规则、入口寄存器约定、IDT/syscall vector、Legacy BIOS disk layout 或现有 VGA text fallback。

## Goals / Non-Goals

**Goals:**

- 在 BootInfo v2 中定义 framebuffer metadata section 和 font asset metadata section。
- 让 UEFI loader 通过 GOP 获取一个线性 framebuffer，并在进入内核前写入可校验 handoff section。
- 以 `assets/fonts/unifont_all-17.0.04.hex` 作为源字体资产，构建阶段生成 `build/assets/fonts/unifont.bin`，打包阶段放入 ESP 的 `/boot/fonts/unifont.bin`，UEFI loader 从 ESP 运行时路径加载后通过 font metadata 把地址、大小、格式版本和度量传给内核。
- 让内核早期解析并保存 framebuffer/font metadata，供后续 framebuffer console backend 使用。
- 把 framebuffer 物理范围作为 firmware/MMIO/device 类保留区域处理，避免加入普通 RAM allocator。
- 建立明确的 device/MMIO mapping API 作为后续 framebuffer 写入的唯一入口，避免 console backend 直接依赖 direct map 写设备内存。
- 保持 Legacy BIOS 文本控制台、COM1 诊断和默认 bounded userland validation 可运行。

**Non-Goals:**

- 不实现 glyph renderer、软件光标、scrollback 的 framebuffer 后端或 UTF-8/codepoint cell 模型。
- 不实现完整字体转换管线；首个源字体资产固定为 `assets/fonts/unifont_all-17.0.04.hex`，首个生成产物固定为 `build/assets/fonts/unifont.bin`，ESP 内运行时路径固定为 `/boot/fonts/unifont.bin`，格式仍保持版本化和最小可校验。
- 不新增 Legacy BIOS VBE backend、Secure Boot、ACPI table handoff、UEFI Runtime Services、动态模式切换或新存储/设备驱动。
- 不改变 kernel virtual address layout、direct map 策略、BootInfo v1 fallback、Legacy BIOS 启动介质布局或用户态 ABI。

## Decisions

1. 使用 BootInfo v2 optional section 表达 framebuffer 和 font metadata。

   理由：现有 handoff ABI 已经采用 tagged sections，新增 section 可以保持旧 consumer 跳过未知可选 section 的兼容语义。相比把字段塞进 `BootInfoCore`，optional section 不会扩大 core 的强制解析面，也便于后续 font pipeline 和 framebuffer renderer 独立演进。

   备选方案：直接让内核探测 UEFI GOP 或复用 loader metadata。前者违反 kernel 不依赖 UEFI Boot Services 的约束；后者会把诊断 metadata 和运行时可消费 metadata 混在一起，校验边界不清晰。

2. UEFI loader 在 `ExitBootServices` 前固定 GOP 模式，内核只消费最终几何。

   理由：GOP 属于 UEFI Boot Services 能力，退出后内核不应调用固件接口；模式确认、像素格式识别和 framebuffer base/size 获取必须在 loader 完成。首版接受 firmware 当前默认模式，只记录实际 geometry/format，不引入偏好分辨率或模式切换策略。内核只看到 normalized metadata，可以避免固件耦合。

   备选方案：进入内核后再切换模式，或在 loader 中按偏好分辨率遍历并切换 GOP mode。前者需要保留 Boot Services 或 Runtime Services 依赖，和当前启动边界不一致；后者会把 renderer 尚未提出的分辨率策略提前固化，首版只保留为后续优化。

3. framebuffer memory 不进入普通 RAM allocator。

   理由：framebuffer 是设备/firmware 映射，不是可分配普通 RAM。即使物理地址落在较高地址空间，也必须通过 memory map 规范化或显式 reserved range 排除，避免 buddy 把显存页分配给内核对象或用户页。

   备选方案：依赖 UEFI memory map 原始类型自然排除。该方案对 firmware 描述质量过度乐观，缺少 framebuffer section 与 allocator review 的交叉校验。

4. 首个字体资产由 UEFI loader 从 ESP 加载，并通过 BootInfo v2 font metadata 传递。

   理由：UEFI backend 已经围绕 ESP payload 启动，loader 读取字体文件可以让字体资产独立于 kernel ELF 更新，并保持后续字体管线产物的替换边界清晰。仓库中的源字体资产放在 `assets/fonts/unifont_all-17.0.04.hex`，构建阶段生成 `build/assets/fonts/unifont.bin`，打包阶段复制到 ESP 的 `/boot/fonts/unifont.bin`；loader 只依赖 ESP 内运行时路径，必须在 `ExitBootServices` 前完成读取、分配保留 buffer，并在 font metadata 中传递物理地址、大小、格式版本、度量和 loader-provided flag；内核只校验并保存引用，不在本 change 中建立完整 glyph lookup。

   备选方案：把最小字体 blob 链接进 kernel `.rodata`，或把字体转换和 GOP handoff 合并。前者启动路径更简单，但会把字体资产发布绑定到 kernel ELF；后者会扩大验证面，并把 boot ABI、构建资产管线和 console rendering 三个风险区域绑在同一次改动里。

5. framebuffer 后续写入必须经过明确的 device/MMIO mapping API。

   理由：framebuffer 是设备/firmware 映射，不能长期假设 direct map 的普通 RAM 访问语义、cache 属性或写合并策略适合它。本 change 建立最小 device/MMIO mapping API 边界，让后续 framebuffer console backend 通过显式 API 获取可写虚拟地址，并把 cache/write-combining 属性作为参数或记录的一部分。

   备选方案：console backend 直接用 direct map/`phys_to_virt` 写 framebuffer。该方案实现最快，但会模糊 RAM 和 device memory 的边界，也不利于后续调整 PAT/MTRR/cache policy 或处理不在 direct map 可写范围内的设备区域。

6. Legacy BIOS 缺失 framebuffer section 是正常 fallback，不是错误。

   理由：Legacy BIOS path 的稳定性是交叉验证 baseline。对于非 UEFI 或不提供 framebuffer metadata 的 backend，内核继续使用现有 VGA text/serial 路径；只有 section 存在但格式非法时才应拒绝或忽略该 section 并记录诊断。

## Risks / Trade-offs

- [Risk] GOP 模式选择在不同 OVMF/QEMU 配置下返回不同分辨率或像素格式 → Mitigation: 只承诺记录实际选择的 geometry/format，验证检查 section 自洽性，不依赖固定分辨率。
- [Risk] framebuffer 物理范围与 memory map 分类不一致 → Mitigation: loader 写入 explicit framebuffer range，内存初始化 review 必须确认该范围未进入 usable free pool。
- [Risk] ESP 字体文件读取、buffer ownership 或保留范围处理错误 → Mitigation: loader 使用 bounded 文件大小、固定格式版本校验和保留内存分配；font metadata 无效时保持启动 fallback，不能破坏 bounded userland baseline。
- [Risk] device/MMIO mapping API 过早扩大虚拟内存修改面 → Mitigation: 首版 API 保持最小能力，只覆盖显式物理范围、大小和属性输入，不改变现有 kernel link address、direct map 或用户地址空间布局。
- [Risk] BootInfo v2 section 数量和总大小超过当前固定上限 → Mitigation: 保持 metadata 结构紧凑，构建期校验 struct size/offset，loader 生成前做容量检查并显式失败。
- [Risk] 后续 console backend 误以为 framebuffer 总是存在 → Mitigation: 内核 API 明确返回 optional view，Legacy/serial fallback 继续作为无 framebuffer 的合法路径。
- [Risk] headless UEFI smoke 难以直接观察像素输出 → Mitigation: 验证以 serial/loader diagnostics 和 BootInfo parsing evidence 为主，图形人工验证作为后续 framebuffer renderer 阶段的补充。

## Migration Plan

1. 先扩展公共 BootInfo v2 header，加入 framebuffer/font metadata section type、结构体和 layout static assertions。
2. 扩展 UEFI loader：定位 GOP、接受 firmware 当前默认模式、填充 framebuffer metadata，并把 framebuffer range 纳入保留审查。
3. 扩展内核 handoff parser：校验 optional section，保存早期只读 metadata view，缺失时保持 fallback。
4. 保持源字体资产位于 `assets/fonts/unifont_all-17.0.04.hex`，构建阶段生成 `build/assets/fonts/unifont.bin`，打包阶段复制到 ESP 的 `/boot/fonts/unifont.bin`；让 UEFI loader 从该 ESP 运行时路径加载首个字体资产，写入 loader-provided font metadata，并确保退出 Boot Services 后资产 buffer 仍被保留。
5. 建立最小 device/MMIO mapping API，供后续 framebuffer writer 使用；本 change 只验证 API 边界和 framebuffer 不经 direct map 被直接写入。
6. 更新 OpenSpec/文档和验证记录；运行构建、源码级检查、UEFI headless smoke，以及 Legacy BIOS fallback 检查。

Rollback 策略：如果 UEFI framebuffer handoff 不稳定，可保留结构定义但让 loader 不产生 optional section，内核会回到现有 VGA text/serial fallback；不得回滚 BootInfo v2 required core/memory map 语义或 Legacy backend。

## Open Questions

当前无阻塞性设计问题。后续 framebuffer renderer 阶段仍需决定字体资产的完整格式、glyph lookup 结构、PAT/MTRR/write-combining 细节和是否引入显式 GOP mode selection policy。
