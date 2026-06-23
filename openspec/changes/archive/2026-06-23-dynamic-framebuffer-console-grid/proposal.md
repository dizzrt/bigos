## Why

当前 framebuffer console 仍复用 Legacy VGA 的固定 80x25 text grid，因此在高分辨率 GOP framebuffer 上只使用左上角一小块区域，固件图形残留也可能留在未重绘区域。现在需要让 framebuffer backend 按实际 framebuffer 尺寸计算可见文本行列并清空整块 framebuffer，使 UEFI 图形路径呈现为完整的 bounded text console。

## What Changes

- 为 framebuffer console 引入动态 text grid：根据已验证 framebuffer 几何、glyph/cell metrics 和固定边界上限计算可见列数与行数。
- framebuffer backend 初始化成功时清空整块映射 framebuffer 背景，而不是只清理 80x25 grid 覆盖区域。
- 将 runtime console 的 visible grid、cursor clamp、viewport redraw、PageUp/PageDown 步长、bottom-follow 和 clear policy 从硬编码 80x25 调整为 backend 提供的可见 grid。
- 保持 Legacy VGA text fallback 继续使用 80x25 固定 grid，不要求 Legacy BIOS 图形模式、VBE 或动态分辨率。
- 继续复用 console-owned scrollback、Unicode/codepoint cell、双宽 cell 和 render backend 边界；不引入完整图形终端、字体缩放、窗口系统或 ANSI/VT 终端仿真。

## Capabilities

### New Capabilities

- `dynamic-framebuffer-console-grid`: 定义 framebuffer console 根据 framebuffer 几何动态计算 visible text grid、执行 full framebuffer clear、并与 console-owned scrollback/viewport 交互的能力边界。

### Modified Capabilities

- `framebuffer-console-backend`: 将 framebuffer backend 从固定 80x25 viewport 扩展为可报告动态 visible grid，并要求 full framebuffer clear 与动态 grid bounds 校验。
- `minimal-terminal-abstraction`: 将默认 runtime console 的可见 viewport 从全局固定 80x25 扩展为由当前 display backend 提供的有界 visible grid，同时保持 Legacy VGA fallback 80x25 和最小终端语义。

## Impact

- 影响子系统：`kernel/core/terminal/console.cc` 的 visible grid/scrollback/viewport/cursor 逻辑、`include/bigos/console_render.h` 的 backend 边界、`kernel/core/terminal/console_render.cc` 的 framebuffer backend 初始化与 full clear、VGA text fallback、源码级 tests、UEFI/framebuffer 文档与验证记录。
- 架构假设：首版只面向 x86_64 UEFI GOP linear framebuffer；Legacy BIOS/VGA text backend 继续固定 80x25。
- 内存布局假设：不改变 framebuffer 物理范围保留、device/MMIO 映射、direct-map 排除、page-table layout、kernel link address、CR3 切换规则或 BootInfo ABI；full clear 只能写入已校验并映射的 framebuffer byte range。
- 磁盘与启动假设：不改变 Legacy MBR/exFAT 路径、ESP 字体路径、UEFI loader 字体 handoff、用户态 ELF 包装、syscall vector `0x80` 或用户态 ABI。
- 工具链和 emulator 假设：优先用源码级检查、默认 `xmake` 构建、QEMU headless Legacy fallback smoke 和 QEMU/OVMF 图形验证组合验证；缺少 cross-toolchain、QEMU、OVMF 或图形能力时必须记录 skipped/blocked 与剩余风险。
- 非目标：不实现动态字体缩放、抗锯齿、居中/边距布局、dirty rectangle 优化、双缓冲、硬件加速、窗口系统、ANSI/VT escape parser、颜色属性状态机、locale/shaping、输入法、多终端、完整 POSIX terminal、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI 与 Legacy 存储/设备 backend parity。
