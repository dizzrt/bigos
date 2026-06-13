## Context

当前 `link.lds` 的实现退回到单个 `kernel PT_LOAD FLAGS(7)`，导致 `build/kernel` 只有一个 `RWE` loadable segment，`x86_64-elf-ld` 在 `xmake` 中报告 `LOAD segment with RWX permissions`。这与既有 `kernel-elf-segment-layout` 规格、`x86-bootloader-hardening` 规格以及 `docs/en/arch/x86-boot-layout.md` / `docs/zh/arch/x86-boot-layout.md` 中记录的三段权限布局不一致。

当前 Legacy BIOS bootloader 规格已经要求按 ELF64 `PT_LOAD` program header 遍历加载 kernel，并支持 text/rodata/data 多段。这个 change 的主要工作不是设计新 boot ABI，而是修复当前 linker script，使实现重新满足已有规格，并用静态 ELF 检查和启动 smoke 确认 boot path 未回归。

## Goals / Non-Goals

**Goals:**

- 将 kernel ELF loadable segments 拆回 `RX` text、`R` rodata、`RW` data/bss 三类权限，消除 `RWE PT_LOAD` 和 linker RWX warning。
- 保持 `0xffffffff80000000` higher-half kernel base、`_start` entry、BootInfo handoff ABI、Legacy BIOS raw image、MBR/exFAT/ATA PIO 加载契约不变。
- 确认 bootloader 能继续遍历多个 `PT_LOAD`，正确加载、zero-fill 并计算 kernel memory extent。
- 增加或恢复源码级/行为级验证，避免未来再次退回单段 RWX linker layout。

**Non-Goals:**

- 不启用运行时 kernel page-table W^X，不改变 direct map、kernel text/rodata/data 页属性或 CR3 切换策略。
- 不新增 UEFI backend，不改 OVMF/ESP/FAT 镜像生成，不引入跨架构 boot layout。
- 不改变 bootloader 固定地址、链接地址、IDT/syscall vector、GDT/TSS、页表 self-map 或用户态 ABI。
- 不改变 `.ctors/.dtors` 的运行时语义；本 change 仍可将其放在 `RW` data segment，以避免同时更改 C++ runtime 表的可写性假设。

## Decisions

- Decision: 在 `link.lds` 中恢复三个显式 `PHDRS`：`text PT_LOAD FLAGS(5)`、`rodata PT_LOAD FLAGS(4)`、`data PT_LOAD FLAGS(6)`。
  Rationale: 这是最小修复，直接消除 linker RWX warning，并与既有规格/文档一致。
  Alternatives considered: 保持单段 RWX 并忽略 warning；拒绝，因为它违反既有规格且会掩盖后续 W^X 工作。

- Decision: 保持所有权限类别边界 4 KiB 对齐。
  Rationale: 未来如果内核页表按 linker boundary 收敛权限，4 KiB 对齐避免同一页同时承载可执行和可写内容。
  Alternatives considered: 仅依赖 section 自然排列；拒绝，因为可能把不同权限内容放在同一页，削弱此 change 的长期价值。

- Decision: `.bigos`、`.init`、`.text`、`.fini` 属于 `text`，`.rodata`、`.rodata1`、`.eh_frame*` 属于 `rodata`，`.ctors`、`.dtors`、`.data`、`.4k_area`、`.bss` 属于 `data`。
  Rationale: `_start` 和启动代码必须可执行；只读表和 unwind 元数据不应可写/可执行；构造/析构表保持在可写 data 段以避免引入额外 runtime 假设变更。
  Alternatives considered: 将 `.ctors/.dtors` 放入 `rodata`；暂缓，因为需要确认当前 freestanding C++ runtime 是否会写这些表或依赖可写映射。

- Decision: 验证重点放在 `readelf`/`objdump` 静态检查和 Legacy BIOS boot smoke。
  Rationale: 该 change 改变的是 ELF program header 和 bootloader 读取结果，静态检查能直接证明无 `RWE`；启动 smoke 能证明多段 kernel ELF 仍可引导。
  Alternatives considered: 只跑 `xmake`；不足，因为 `xmake` 只能看到 linker 是否警告，不能证明段权限、entry、section-to-segment 关系和启动行为。

## Risks / Trade-offs

- [Risk] bootloader 实现仍隐含单 `PT_LOAD` 假设 -> Mitigation: 审查 `kernel/arch/x86/boot` ELF loader，并通过 QEMU/Bochs smoke 验证多段 kernel ELF 可启动。
- [Risk] `.bss` 或 `.4k_area` segment `p_memsz > p_filesz` zero-fill 行为回归 -> Mitigation: 保留/补充针对 `p_memsz > p_filesz` 和 kernel extent 计算的源码级检查。
- [Risk] 拆段改变 kernel file size 或 segment alignment，暴露 boot image size/loader bounds 问题 -> Mitigation: 运行 `xmake` image build 和至少一个 headless emulator marker check。
- [Risk] 静态文档已经描述三段布局，但实现回归 -> Mitigation: 增加 source-contract test 直接读取 `link.lds` 和 `readelf` 输出，作为未来防回归边界。

## Migration Plan

- 先确认 bootloader 当前按 `PT_LOAD` 遍历加载 kernel，并记录是否需要补充测试。
- 修改 `link.lds` 的 `PHDRS` 和 section-to-segment 映射，保持地址、entry 和 alignment 稳定。
- 运行 `xmake`，确认不再出现 `LOAD segment with RWX permissions`。
- 运行 `x86_64-elf-readelf -lW build/kernel` 或 `x86_64-elf-objdump -p build/kernel`，记录 `LOAD` 权限为 `R E`、`R`、`RW`，且无 `RWE`。
- 运行 QEMU headless smoke；可用时运行 Bochs smoke 或记录 Bochs 环境限制。
- 若拆段导致 bootloader 无法加载，回滚 `link.lds` 变更并先修复 bootloader 多 `PT_LOAD` 支持，不保留半工作的多段布局。

## Open Questions

- 是否需要在本 change 中新增 linker boundary symbols 给未来 kernel page-table W^X 使用？默认不做，除非实现时发现测试需要稳定符号。
- Bochs 环境是否能稳定跑 headless serial marker？如果本地 ROM/display/交互限制不可控，允许记录 QEMU 通过、Bochs 跳过原因和残余风险。
