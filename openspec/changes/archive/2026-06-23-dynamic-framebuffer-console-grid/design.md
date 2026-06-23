## Context

BigOS 已有 runtime console、framebuffer text backend、kernel glyph lookup 和 Unicode/codepoint cell model。当前 framebuffer backend 成功选择后仍按 Legacy VGA 兼容的 80x25 grid 渲染，通常只覆盖 640x400 左上角区域；GOP framebuffer 的剩余区域不会被 console full clear，可能保留 OVMF/固件图形残留。

该 change 的目标不是引入完整图形终端，而是把现有 bounded text console 的可见 grid 从固定 80x25 调整为 backend-provided visible grid：VGA backend 仍报告 80x25，framebuffer backend 根据 framebuffer 尺寸和 cell metrics 报告 `cols x rows`，并在初始化/clear 时清空整块映射 framebuffer。

目标数据流：

```text
validated framebuffer metadata + glyph cell metrics
        |
        v
framebuffer backend probe: map MMIO, compute cols/rows, validate byte bounds
        |
        v
console visible grid = backend.visible_grid()
        |
        v
console state: scrollback, cursor, viewport, Unicode cells
        |
        v
backend redraw: full framebuffer clear + dynamic visible grid paint
```

## Goals / Non-Goals

**Goals:**

- framebuffer backend 根据 framebuffer width/height 和 glyph cell_width/cell_height 计算动态列数和行数。
- framebuffer backend 初始化和 console clear 路径清空整块 mapped framebuffer，避免固件图形残留留在未使用区域。
- runtime console 使用当前 backend 的 visible grid 进行 cursor clamp、line wrapping、viewport redraw、PageUp/PageDown step 和 bottom-follow。
- Legacy VGA text backend 保持固定 80x25 行列、硬件光标和现有 fallback 语义。
- 保持 scrollback 历史容量固定有界，不引入动态增长或 filesystem persistence。
- 补充源码级、构建级、Legacy fallback smoke 和 QEMU/OVMF 图形验证记录。

**Non-Goals:**

- 不实现字体缩放、抗锯齿、居中/边距布局、主题背景、dirty rectangle、双缓冲、硬件加速或窗口系统。
- 不实现 ANSI/VT escape parser、颜色属性状态机、termios、多终端、完整 POSIX terminal、locale、shaping、输入法或完整 Unicode terminal。
- 不改变 BootInfo ABI、framebuffer metadata ABI、UEFI loader 字体路径、kernel link address、page-table layout、CR3 切换规则、IDT/syscall vector、磁盘布局或用户态 syscall ABI。
- 不要求 Legacy BIOS 图形 backend、VBE、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI/Legacy storage parity。

## Decisions

1. **backend 报告 visible grid，console 使用该 grid。**

   - 决策：在 internal render backend 边界中加入 visible columns/rows 查询或等价初始化结果。VGA backend 固定返回 80x25；framebuffer backend 返回由 framebuffer 几何和 cell metrics 计算出的 grid。
   - 理由：visible grid 是显示 backend 属性；让 console 直接硬编码 framebuffer 尺寸会破坏 backend 边界。
   - 替代方案：让 console 直接读取 framebuffer metadata。该方案会让 console 状态依赖 UEFI/GOP 细节，也会污染 Legacy fallback 边界。

2. **首版使用整数 cell tiling，不做缩放和居中。**

   - 决策：`cols = framebuffer_width / cell_width`，`rows = framebuffer_height / cell_height`，只使用完整 cell 覆盖区域；右侧或底部不足一个 cell 的像素在 full clear 后保持背景色。
   - 理由：整数 tiling 简单、可验证，避免 glyph scaling、baseline、抗锯齿和非整数 bounds 的复杂性。
   - 替代方案：按 framebuffer 尺寸缩放 glyph 或居中 80x25 grid。缩放会引入 renderer 复杂度；居中不能解决可见行列不足的问题。

3. **设置有界最大 grid，防止 framebuffer 尺寸放大静态存储。**

   - 决策：动态 grid 仍受编译期最大列数/行数限制，例如 `MAX_CONSOLE_COLUMNS` 和 `MAX_CONSOLE_ROWS`。framebuffer 计算结果超过上限时 clamp 到上限，并记录源码级验证边界。
   - 理由：kernel console state 当前是静态/固定容量，不能因固件报告超大分辨率而动态分配或增长栈/全局内存到不可控。
   - 替代方案：按 framebuffer 动态分配 console cell buffer。该方案会把 console 初始化绑定到 allocator 生命周期，扩大失败路径，不适合当前阶段。

4. **full framebuffer clear 只写已校验的 mapped byte range。**

   - 决策：framebuffer backend 在选择成功后通过现有 MMIO mapping 遍历 `height x pixels_per_scanline` 或已验证 byte range 清成背景色；后续 console clear 也清空整块 framebuffer，再重置 console state。
   - 理由：固件残留本质是未写像素区域；只清 grid 无法给用户一个完整 console 画面。
   - 替代方案：只清动态 grid 使用区域。该方案在右侧/底部不足一个 cell 的像素仍可能残留固件图像。

5. **scrollback 容量固定，visible viewport 高度动态。**

   - 决策：scrollback 总行数继续固定 256 行；可见 rows 由 backend grid 决定。PageUp/PageDown step 使用 `visible_rows - 1`，bottom viewport 也按 visible rows 计算。
   - 理由：动态 rows 是显示能力；历史容量是 console retention policy。两者分离能保持内存边界稳定。
   - 替代方案：按可见 rows 放大 scrollback 容量。该方案改变历史保留语义并增加静态内存压力。

## Risks / Trade-offs

- [Risk] 大 framebuffer full clear 可能较慢。Mitigation: 首版接受启动/clear 时的线性清屏成本，保持实现简单；后续可在 backend 内优化，但不得改变 console state 语义。
- [Risk] 动态 columns/rows 与 scrollback ring 组合可能引入 cursor 或 viewport off-by-one。Mitigation: 增加源码级测试覆盖 grid clamp、PageUp/PageDown step、bottom viewport 和 line wrap。
- [Risk] 超大分辨率导致静态 cell buffer 过大。Mitigation: 使用编译期最大 grid，上限外 clamp，不做动态增长。
- [Risk] full clear 越过 framebuffer 映射。Mitigation: 使用已验证的 stride、height、bytes_per_pixel 和 mapping.length 计算写入边界；任何溢出或不一致都回退 VGA。
- [Risk] QEMU/OVMF 图形验证依赖本地固件和显示能力。Mitigation: 记录缺失工具为 skipped/blocked；以源码级检查、构建和 Legacy smoke 覆盖非图形路径。

## Migration Plan

1. 扩展 internal console render backend 边界，提供 visible columns/rows 和 full clear 能力。
2. 将 console state 的 visible width/height 从固定 80x25 改为 backend-provided grid，同时保留固定最大容量。
3. 更新 framebuffer backend probe：计算 dynamic grid、校验 grid pixel bounds、清空整块 framebuffer。
4. 更新 viewport redraw、cursor、line wrap、PageUp/PageDown、clear 和 bottom-follow 使用动态 grid。
5. 更新 docs/en 与 docs/zh，补充 source-level tests、默认 build、Legacy smoke 和 QEMU/OVMF 图形验证记录。

回滚策略：如果动态 grid 引入启动或 console 可用性回归，可将 framebuffer backend 临时固定报告 80x25，同时保留 full framebuffer clear；若 backend boundary 本身不稳定，则回退到当前固定 `CONSOLE_RENDER_WIDTH/HEIGHT` 接口。

## Open Questions

- 首版最大 framebuffer console grid 上限取值需要在实现时结合静态内存预算确定；建议优先使用保守上限并通过源码级测试固定。
