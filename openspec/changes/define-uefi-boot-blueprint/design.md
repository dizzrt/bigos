## Context

BigOS 当前支持的启动路径是 x86 Legacy BIOS：BIOS 加载 MBR，MBR/DBR/exDBR 读取 exFAT 中的 `/boot/boot.bin`，`boot.bin` 进入 long mode、加载 root `kernel` ELF64、建立早期页表并跳转到 higher-half kernel entry。该路径已经通过 `BootInfo` 暴露部分启动元数据，内存模块也开始优先读取 `BootInfo`，但实际数据来源仍主要是 BIOS E820、BIOS boot drive、ATA PIO 和固定 exFAT 布局。

UEFI 与当前路径的关键差异不在 kernel ELF 本身，而在 loader 入口、固件服务、磁盘读取、内存图来源、图形输出和镜像格式。若后续模块继续直接绑定 BIOS E820、VGA text、MBR/exFAT 和 Bochs BIOS 调试入口，未来转向 UEFI 会要求跨内存、显示、ACPI、调试工具和 boot ABI 的大规模返工。

本 change 的定位是先建立蓝图和项目级规划，不实现 UEFI loader。它应让后续模块在设计时有明确约束：kernel 消费统一 handoff，boot backend 负责把 BIOS 或 UEFI 固件数据规范化。

## Goals / Non-Goals

**Goals:**

- 定义 UEFI 与现有 BIOS 路径并行演进的启动架构蓝图。
- 明确 `kernel()` 及其后续模块不直接依赖 BIOS interrupt 或 UEFI Boot Services。
- 规划 `BootInfo` 后续版本，支持统一内存图、boot protocol、framebuffer、firmware tables 和 loader metadata。
- 规划内存模块迁移方向：从读取 E820 兼容地址演进到读取标准化 `BootMemoryRegion` 数组。
- 明确现有 `make boot-debug`、MBR/DBR/exDBR/`boot.bin` 和 raw exFAT 镜像在蓝图阶段继续兼容。
- 将当前不实现但后续阶段要做的 UEFI loader、ESP 镜像、OVMF/QEMU 调试入口、GOP/ACPI 支持整理为项目级路线图。

**Non-Goals:**

- 不在本 change 中实现 `BOOTX64.EFI`。
- 不在本 change 中改变 `make boot-debug` 的 Legacy BIOS 语义。
- 不在本 change 中移除 MBR、DBR、exDBR、`boot.bin` 或现有 Bochs BIOS 调试路径。
- 不在本 change 中改变 kernel ELF 链接地址、higher-half base、ELF 文件名 `kernel` 或当前运行时代码；未来 kernel entry ABI 将规划为寄存器传递 `BootInfo*`。
- 不在本 change 中要求 kernel 调用 UEFI Boot Services、Runtime Services 或 BIOS interrupt。
- 不在本 change 中引入外部 UEFI 库、hosted runtime、异常、RTTI 或非 freestanding 依赖。

## Decisions

### Decision: UEFI 作为并行 boot backend，而不是替换 BIOS 路径

当前 BIOS 路径仍是可运行的低层调试路径，并且 `make boot-debug` 已围绕该路径形成稳定开发入口。UEFI 接入应新增并行路径，最终与 BIOS 路径在进入 kernel 前统一生成同一类 handoff 数据。

```text
Legacy BIOS path                         UEFI path
────────────────                         ─────────
MBR -> DBR -> exDBR -> boot.bin          BOOTX64.EFI
              │                              │
              ├─ BIOS E820                  ├─ UEFI GetMemoryMap
              ├─ VGA text                   ├─ GOP framebuffer
              ├─ ATA/exFAT                  ├─ SimpleFileSystem/ESP
              │                              │
              └──────── normalize ──────────┘
                           │
                      BootInfo v2+
                           │
                        kernel()
```

Alternatives considered:

- 替换 `make boot-debug` 为 UEFI：会破坏现有 boot-debug spec、Bochs BIOS workflow 和低层启动调试能力。
- 保持完全分叉的 BIOS/UEFI kernel entry：会把 firmware 差异扩散到内存、显示和驱动模块，长期维护成本更高。

### Decision: kernel 消费统一 handoff，不消费固件原始协议

BIOS E820 和 UEFI `GetMemoryMap` 都应由对应 loader 转换为 kernel 可理解的统一 memory map。kernel 侧不应直接解释 UEFI descriptor，也不应继续新增对 BIOS 魔法地址的依赖。

建议的长期数据流：

```text
BIOS E820 entry[]             UEFI EFI_MEMORY_DESCRIPTOR[]
       │                                  │
       ▼                                  ▼
BIOS loader normalize          UEFI loader normalize
       │                                  │
       └────────── BootMemoryRegion[] ────┘
                          │
                          ▼
                    bigos::mm::init_mem()
```

统一内存区域至少需要表达：

- base physical address
- length in bytes
- normalized type，例如 usable、reserved、acpi_reclaim、acpi_nvs、mmio、loader、kernel、bad_memory
- attributes，例如 cacheability、runtime、write-back/write-combine、firmware-specific flags

Alternatives considered:

- 让内存模块同时支持 E820 和 UEFI descriptor：短期方便，但会让固件差异进入 allocator 和 VM 层。
- 只保留 E820 语义并把 UEFI descriptor 压成 E820：简单但丢失 UEFI runtime、attribute、ACPI/NVS 等信息，不利于后续虚拟内存和 ACPI。

### Decision: BootInfo 长期采用 header 加 tagged sections

现有 `BootInfo` 已有 magic、version、size 和字段 offset 校验，是很好的 ABI 基础。长期方向确定为 `BootInfoHeader + tagged sections`，因为 UEFI 会引入 memory map、GOP framebuffer、ACPI/SMBIOS tables、loader metadata 等可选和变长数据，固定 struct 会快速膨胀并增加 ABI 破坏风险。

推荐方向：

- 下一阶段先定义 `BootInfoHeader` 和少量固定 core 字段，例如 magic、version、header size、total size、flags、boot protocol 和 section table metadata。
- 后续逐步把 memory map、framebuffer、firmware tables 和 loader metadata 放进 tagged sections。
- 每个 section 必须携带 type、flags、offset、size 或等价边界信息，kernel consumer 必须校验长度、对齐和越界。
- 未识别的非必需 section 可以跳过；必需 section 缺失或格式错误必须导致早期失败或显式 fallback。
- BIOS backend 在迁移期可以继续生成现有 v1 `BootInfo` 兼容数据，但新 backend 应以 header + sections 为目标 ABI。

Alternatives considered:

- 直接修改现有 `BootInfo` 字段含义：实现量小，但容易让旧 BIOS 路径和新 consumer 对同一字段产生不同解释。
- 使用多个完全独立 handoff struct：短期清晰，但 kernel 入口需要区分启动来源，违背统一 handoff 目标。
- 固定 struct 继续扩展所有字段：C/C++ 访问简单，但不适合可选、变长和多来源的 UEFI 数据。

### Decision: UEFI loader 负责退出固件服务并建立 kernel 入口环境

未来 UEFI loader 应在进入 kernel 前完成 `GetMemoryMap`、必要的文件读取、页表准备、BootInfo 填充和 `ExitBootServices()`。kernel 不应在常规初始化路径中调用 UEFI Boot Services。

kernel entry 环境应保持可测试的低层约束：

- CPU 已在 x86_64 long mode。
- 分页已开启，并包含 kernel higher-half 映射和必要的早期物理/identity 映射。
- 栈可用且满足 ABI 对齐要求。
- 中断关闭，IDT 状态由 kernel 初始化流程接管。
- `BootInfo*` 通过约定寄存器传递给 kernel entry，例如 x86_64 System V 风格使用 `rdi` 传递第一个参数。
- 固定低地址 `BootInfo` 只作为 Legacy BIOS 迁移期 fallback 或调试兼容手段，不作为长期主 ABI。

Alternatives considered:

- kernel 自己调用 UEFI services：需要把 UEFI ABI、handle、system table 和服务生命周期引入 kernel，且 `ExitBootServices()` 后行为更复杂。
- UEFI loader 只做 chainload 到现有 `boot.bin`：可以复用现有 ELF loader，但会同时承受 UEFI 和 BIOS boot-stage 假设，价值有限。
- 长期固定从低地址读取 `BootInfo`：兼容当前 BIOS 实现，但会把 UEFI backend 绑定到 BIOS 低地址布局。

### Decision: UEFI loader 单独实现 ELF reader，共享 ELF64 加载规则

UEFI loader 不直接复用现有 BIOS `boot.cc` 中的 ELF loader 代码。当前 `boot.cc` 混合了 ATA PIO、exFAT 查找、固定低地址、页表准备和 `BootInfo` 写入，强行复用会把 BIOS 设备和文件系统假设带入 UEFI 路径。UEFI loader 应围绕 UEFI Simple File System、UEFI 内存分配和 `ExitBootServices()` 顺序单独实现 ELF reader。

但 BIOS 与 UEFI 必须共享同一套 ELF64 加载行为规范：

- 只支持 ELF64 x86_64 executable kernel。
- 校验 ELF header、program header table 边界、`PT_LOAD` segment 文件范围和内存范围。
- 按 `PT_LOAD` segment 加载到预期虚拟/物理映射目标。
- 对 `p_memsz > p_filesz` 的 segment 执行 zero-fill。
- 校验 `e_entry` 落在已加载 segment 内。
- 拒绝不受支持或越界的 ELF，而不是尝试容错执行。

Alternatives considered:

- 直接共享现有 `boot.cc` ELF 加载代码：减少重复，但需要先大幅拆分 BIOS 读盘和 exFAT 逻辑，短期风险高。
- 让 UEFI loader 仅加载平坦二进制 kernel：实现简单，但会放弃现有 ELF64 kernel 构建和 segment 语义。

### Decision: 近期不支持 UEFI Runtime Services，但保留 runtime metadata

未来 UEFI loader 在 `ExitBootServices()` 后不向 kernel 暴露可调用的 UEFI Runtime Services API，kernel 也不在本路线图近期阶段调用 UEFI Runtime Services。Runtime Services 涉及 `EFI_MEMORY_RUNTIME` 映射、虚拟地址切换、`SetVirtualAddressMap()`、页表属性和固件兼容性，复杂度明显超过当前 kernel bring-up 所需。

同时，UEFI memory map 转换为 `BootMemoryRegion` 时必须保留 runtime memory 类型和 attributes，不能把 `EFI_MEMORY_RUNTIME`、cacheability、write-back/write-combine 等信息丢弃。这样未来如果需要 UEFI variables、reset、time 或其它 runtime service，可以基于已有 metadata 单独设计。

Alternatives considered:

- 立即支持 Runtime Services：功能完整，但会显著增加 UEFI loader、页表和内核 ABI 复杂度。
- 完全丢弃 runtime memory 信息：实现最简单，但会导致未来 Runtime Services 或固件内存诊断返工。

### Decision: 启动调试入口分层命名

`make boot-debug` SHALL 保持 Legacy BIOS 含义，并继续首选 Bochs。未来 UEFI 调试入口使用独立命名，例如 `make uefi-boot-debug` 或 `python3 tools/uefi_boot_debug.py run`。UEFI smoke test 正式首选 QEMU + OVMF，Bochs UEFI 仅作为可选验证路径。

Alternatives considered:

- 用参数切换 `make boot-debug FIRMWARE=uefi`：可扩展但不够直观，容易破坏现有自动化。
- 只保留 UEFI 命令：会丢失当前可工作的 BIOS bring-up 入口。
- UEFI 继续首选 Bochs：能延续现有工具习惯，但 OVMF/QEMU 的 UEFI 生态、headless smoke test 和 CI 可移植性更好。

## Risks / Trade-offs

- [Risk] 蓝图过早抽象，导致当前 BIOS 路径实现负担增加。→ Mitigation：本 change 只落设计、spec 和规划任务，不要求立即改运行时代码。
- [Risk] `BootInfo` 后续扩展破坏早期内存初始化。→ Mitigation：要求版本化 ABI、size 检查、legacy fallback 和构建期 layout 校验。
- [Risk] UEFI memory attributes 被过度简化，后续 VM/MMIO/Runtime Services 需要返工。→ Mitigation：统一 memory map 预留 `attributes`，并在规划中明确映射表需要单独设计。
- [Risk] 同时维护 BIOS 与 UEFI 增加测试矩阵。→ Mitigation：分阶段推进，BIOS 保留 `make boot-debug`，UEFI 后续新增独立 smoke test。
- [Risk] Bochs UEFI 支持和宿主机 OVMF 配置不稳定。→ Mitigation：UEFI 阶段优先规划 QEMU + OVMF，Bochs UEFI 作为可选路径；现有 Bochs BIOS 不受影响。
- [Risk] 未来 UEFI loader 构建需要 PE/COFF 或 objcopy 流程，可能与现有 xmake/toolchain 设计冲突。→ Mitigation：蓝图阶段只记录工具链假设，后续 loader spike 单独验证。

## Migration Plan

1. 本 change 只创建 OpenSpec 设计、spec 和项目级规划任务。
2. 后续 change 先扩展文档和 public boot handoff header，定义 `BootInfo` 后续版本草案和统一 memory map 格式。
3. BIOS 路径继续生成当前 `BootInfo`，并逐步补充可规范化的数据，不改变 `make boot-debug`。
4. 内存模块在独立 change 中迁移到统一 memory map consumer，保留现有 `BootInfo` v1/E820 fallback。
5. UEFI loader 作为 spike 独立实现，目标只到加载 kernel、填充 handoff、进入 `kernel()`。
6. UEFI 调试入口和 ESP 镜像生成在 loader spike 稳定后再纳入 `tools/` 和文档。

Rollback strategy:

- 本 change 是文档和 OpenSpec 蓝图；如方向不合适，可归档或修改 change，不影响当前运行时代码。
- 后续每个实现阶段都应保持 BIOS 路径可回退，并用 `make boot-debug` 作为 Legacy smoke test。

## Resolved Decisions

- `BootInfo` 后续版本长期采用 `BootInfoHeader + tagged sections`；下一阶段先定义 header 和少量固定 core 字段，再逐步把 memory map、framebuffer 等放进 sections。
- kernel entry ABI 长期改为寄存器传递 `BootInfo*`；固定低地址仅作为 Legacy BIOS 迁移期 fallback。
- UEFI loader 单独实现更适合 UEFI 的 ELF reader；BIOS 与 UEFI 共享 ELF64 加载规则规范，不急于共享代码。
- 近期不支持调用 UEFI Runtime Services；memory map 中保留 runtime memory 类型和 attributes。
- UEFI smoke test 首选 QEMU + OVMF；Bochs UEFI 为可选验证路径，Legacy BIOS 继续用 Bochs。
