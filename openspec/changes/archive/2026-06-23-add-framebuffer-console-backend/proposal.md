## Why

UEFI 路径已经能够把 framebuffer 几何信息和内核 glyph lookup 字体资产交给内核，但默认 runtime console 仍只能通过 VGA text backend 呈现。现在需要在不重写上层 console/scrollback 状态的前提下，为图形 framebuffer 提供一个可选文本输出 backend，使 UEFI 图形启动路径具备可见的文本控制台基础。

## What Changes

- 新增 framebuffer text console backend，消费已校验的 framebuffer handoff、显式 device/MMIO 映射和内核 glyph lookup view，在图形 framebuffer 上渲染单色或固定调色的点阵字形。
- 在现有 console 输出接口之后接入 backend 选择，使普通 runtime console 输出、shell stdout/stderr 和已实现的 scrollback/viewport 状态可继续复用。
- 为 framebuffer backend 实现软件光标绘制、擦除和移动，避免依赖 VGA 硬件光标。
- 将现有 console 自动上卷和 scrollback viewport 重绘投递到 framebuffer backend，使图形路径支持确定性整屏重绘和滚动。
- 在 framebuffer 元数据或字体 lookup 不可用、无效或无法映射时，继续使用 Legacy VGA text/serial fallback，不阻塞现有启动与 bounded userland baseline。
- 保持 Unicode 文本模型升级在后续能力中完成；本 change 只按当前 console cell/byte 输出语义渲染已有可表示字符，不实现 UTF-8 decoding 或双宽 cell 布局。

## Capabilities

### New Capabilities

- `framebuffer-console-backend`: 定义 framebuffer 文本 console backend 的初始化、glyph 渲染、软件光标、scrollback/viewport 重绘和 fallback 边界。

### Modified Capabilities

- `minimal-terminal-abstraction`: 将默认 runtime console 的输出 backend 从单一 VGA text backend 扩展为可复用同一 console/scrollback 状态的 backend 边界，同时保持最小终端语义不扩大为完整 ANSI/VT、termios 或 POSIX terminal。

## Impact

- 影响子系统：x86_64 UEFI framebuffer handoff consumer、device/MMIO framebuffer 映射边界、kernel glyph lookup view、runtime console/terminal 输出状态、VGA text fallback、QEMU/OVMF 图形验证路径。
- 架构假设：首版只覆盖 x86_64 UEFI GOP 提供的线性 framebuffer；Legacy BIOS 继续使用 VGA text backend，不新增 BIOS VBE 图形 backend。
- 内存布局假设：framebuffer 物理范围继续作为 firmware/MMIO/device 类区域保留；写入前必须经显式 device/MMIO 映射，不通过普通 RAM allocator 或未经审计的 direct-map 假设访问。
- 磁盘与启动假设：不改变 BootInfo v2 section ABI、ESP 字体路径、Legacy MBR/exFAT 路径、kernel link address、IDT/syscall vectors、page-table layout 或现有用户态 syscall ABI。
- 工具链和 emulator 假设：优先使用 QEMU + OVMF 验证 framebuffer console；缺少 OVMF、QEMU、cross-toolchain 或图形/截图能力时必须记录为 skipped/blocked，并用构建和源码级检查覆盖静态边界。
- 非目标：不实现 UTF-8 decoding、Unicode codepoint cell model、双宽 cell 布局、ANSI/VT escape parser、颜色属性状态机、图形加速、双缓冲、多终端、完整 POSIX terminal、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI 与 Legacy 存储/设备 backend parity。
