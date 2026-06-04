## Context

当前 `link.lds` 使用单个 `PHDRS { All PT_LOAD; }`，并将 `.bigos`、`.text`、`.rodata`、`.ctors`、`.data`、`.bss` 等全部标记为 `: All`。因此 `x86_64-elf-ld` 会把 kernel 产物描述为一个同时可读、可写、可执行的 load segment，并报告 `build/kernel has a LOAD segment with RWX permissions`。

Legacy BIOS bootloader 侧已经按 ELF64 program header table 遍历所有 `PT_LOAD`，并对每段执行目标地址校验、文件范围校验、读取 `p_filesz`、zero-fill `p_memsz - p_filesz`，入口点也校验落在某个已加载的 `PT_LOAD` 内。因此拆分 kernel `PHDRS` 的主要风险不在“是否支持多个 segment”，而在新 segment 的地址对齐、文件偏移、内存范围和 bootloader 现有 higher-half 映射假设是否仍成立。

目标平台仍为 x86_64 higher-half kernel，基址保持 `0xffffffff80000000`，入口仍为 `_start`。本 change 不改变 boot handoff ABI、页表策略、磁盘布局、文件系统读取逻辑或用户态权限模型。

## Goals / Non-Goals

**Goals:**

- 将 kernel ELF 拆成至少 `text`、`rodata`、`data` 三类 `PT_LOAD`，消除 linker 的 RWX LOAD segment warning。
- 让 `.bigos`、`.init`、`.text`、`.fini` 等可执行启动代码进入 RX segment。
- 让 `.rodata*`、只读 `.eh_frame*` 等进入 R segment。
- 让 `.ctors`、`.dtors`、`.data`、`.4k_area`、`.bss` 等进入 RW segment。
- 在 segment 边界使用页对齐，避免未来页表按段权限收敛时出现同一页混合 RX/R/RW 内容。
- 验证 bootloader 继续正确加载拆分后的多个 `PT_LOAD`，并保留 kernel entry、kernel memory size 和 boot handoff 行为。

**Non-Goals:**

- 不启用真正的页级 W^X enforcement；当前 change 只修正 ELF program header 权限布局。
- 不改变 `KERNEL_VMD`、链接基址、`ENTRY(_start)`、boot info 地址或 runtime startup ABI。
- 不重构 Legacy BIOS loader 的文件系统、磁盘读取、E820 或 paging 初始化。
- 不实现 UEFI loader、用户态、syscall、scheduler 或进程权限隔离。

## Decisions

### D1: 使用显式具名 PHDRS

采用三个主要 program header：

```ld
PHDRS {
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}
```

理由：权限意图直观，`readelf -l build/kernel` 可直接验证 `R E`、`R`、`RW`。相比依赖 linker 自动推导，显式 `FLAGS` 更适合 freestanding kernel 的低层布局控制。

备选方案：只拆成 RX 和 RW 两段。该方案能消除 RWX，但 `.rodata` 会与可执行或可写内容共享 segment，不利于后续只读页保护。

### D2: 以页对齐作为 segment 边界

在 `.text` 到 `.rodata`、`.rodata` 到 `.data` 的边界插入 `ALIGN(0x1000)`。这会让 segment virtual address 和 file offset 更容易满足 ELF loader 与未来页权限映射要求。

理由：如果不同权限内容共享同一 4KiB 页，后续页表只能选择更宽松权限。页对齐会略微增大 kernel 文件或内存占用，但换来清晰边界。

备选方案：仅依赖 section 自然对齐。该方案变更更小，但无法保证未来页级权限隔离可用。

### D3: 保持现有 section 顺序和启动入口归属

`.bigos` 中的 `_start` 仍位于最早的可执行 segment，后接 `.init`、`.text`、`.fini`。`.ctors` 和 `.dtors` 保持在 RW segment，避免 C++ runtime 初始化表被误放入只读或可执行 segment 后引入未知兼容性问题。

理由：现有 runtime startup 从 `_start` 保存 handoff 参数、调用 `_init`、进入 `kernel()`；该路径对入口位置和构造/析构表顺序敏感，不应在权限拆分中同时重排语义。

备选方案：将 `.ctors/.dtors` 放入只读 segment。长期可能更合理，但需要先确认当前 crt objects 与写入需求，不作为本 change 的低风险目标。

### D4: Bootloader 只做兼容性复核，必要时小修

实现时先用 `readelf -l build/kernel` 确认拆分后的 `PT_LOAD` 数量、权限、offset、vaddr、filesz、memsz。再复核 `src/arch/x86/boot/boot.cc` 是否仍满足：

- 遍历每个 `PT_LOAD`，不假设只有一个 load segment。
- 校验 `p_filesz <= p_memsz`。
- 校验 `p_vaddr >= KERNEL_VMD` 且 `p_vaddr - KERNEL_VMD + p_memsz` 不越界。
- 按 `p_offset` 读取各段文件内容，并 zero-fill BSS。
- 用所有 `PT_LOAD` 的最大 end 计算 `kernel_memory_size`。

若检查发现 PHDR 拆分暴露 loader 边界问题，只做针对性修复，不扩大到 bootloader 架构重写。

### D5: 验证分为静态 ELF 检查和 boot smoke

静态检查必须覆盖：

- `xmake -r` 成功，且不再出现 `LOAD segment with RWX permissions`。
- `x86_64-elf-readelf -l build/kernel` 显示多个 `LOAD`，权限不包含 `RWE`。
- `Entry point` 仍落在 RX `PT_LOAD`。
- `LOAD` 段的 `VirtAddr` 仍位于 `0xffffffff80000000` 起的 higher-half 范围。

运行时检查优先使用 `uv run python tools/boot_debug.py run --expect-serial-marker ...` 或等价 Bochs smoke。若当前机器仍受 term GUI/serial oracle 限制，必须记录不可用原因，并以 ELF 静态检查、源码检查和交叉构建作为替代证据。

### D6: `.ctors/.dtors` 本 change 保持 RW segment

`.ctors` 和 `.dtors` 在本 change 中继续归入 RW `data` segment，不迁移到只读 `rodata` segment。这样可以避免在修正 ELF `PT_LOAD` 权限布局时，同时改变 legacy C++ runtime 初始化/析构表的可写性假设。

理由：当前链接脚本仍使用 legacy `.ctors/.dtors`、`SORT(CONSTRUCTORS)`、`_init/_fini` 路径；是否能安全迁移到只读 segment，需要先确认 crt objects、构造/析构表遍历方式，以及未来是否切换到 `.init_array/.fini_array`。这些属于 C++ runtime 模型演进，不是本 change 的必要条件。

后续方向：另起 C++ runtime/CRT 初始化模型 change，确认初始化表只读遍历且无运行时写入需求后，再评估将 `.ctors/.dtors` 或其替代 sections 放入只读 segment。

### D7: 运行时页权限后续优先使用 linker 边界符号

本 change 不让 bootloader 或 kernel 页表代码消费 ELF segment flags 来设置运行时页权限。后续若要启用 kernel text/rodata/data 页级权限收敛，优先由 `link.lds` 暴露页对齐的 text、rodata、data 边界符号，再由 kernel virtual memory 初始化按这些边界映射权限。

理由：直接使用 ELF `p_flags` 需要 bootloader 在加载后继续保存并通过 BootInfo 传递 segment metadata，扩大 boot handoff ABI 和 loader/kernel 职责边界。linker 符号方案与当前 higher-half kernel 静态布局更匹配，改动面更小，也更容易和早期虚拟内存初始化顺序对齐。

保留约束：ELF `p_flags` 仍必须正确，用于 `readelf` 静态验证、bootloader 加载契约，以及未来更通用 loader 或 BootInfo segment metadata 演进。

## Risks / Trade-offs

- [Risk] 页对齐会改变部分 section 地址和 kernel image size → [Mitigation] 保持基址和入口不变，验证 entry、segment 范围、`kernel_memory_size` 和 boot smoke。
- [Risk] 拆分后 `p_offset`/`p_vaddr` 对齐暴露 bootloader 读取边界问题 → [Mitigation] 复核现有多 `PT_LOAD` 加载逻辑，并用 readelf/boot smoke 验证每段加载。
- [Risk] `.eh_frame` 或 runtime 表归属错误导致启动或 `_init/_fini` 行为异常 → [Mitigation] 明确 `.eh_frame*` 进入只读段，`.ctors/.dtors` 保持 RW 段，避免同时改变 C++ runtime 语义。
- [Risk] ELF segment 权限改善不等于运行时页权限生效 → [Mitigation] 文档明确本 change 只修正 ELF layout，页表权限收敛另行设计，并优先采用 linker 边界符号方案。
- [Risk] 本机 Bochs 环境不可稳定观测 serial marker → [Mitigation] 记录 emulator 缺口，并保留可复现的 `readelf`、构建和源码不变量检查。

## Migration Plan

1. 修改 `link.lds` 的 `PHDRS` 和 section 到 PHDR 的映射。
2. 使用 `xmake -r` 生成 kernel，并确认 RWX LOAD warning 消失。
3. 使用 cross `readelf` 检查 `PT_LOAD` 数量、权限、entry、vaddr 和 filesz/memsz。
4. 复核或必要时修复 `src/arch/x86/boot/boot.cc` 的多段加载边界。
5. 运行可用的 Bochs serial marker smoke；不可用时记录原因。
6. 更新架构文档，说明 kernel ELF segment 权限布局和非目标。

Rollback 策略：若拆分后 boot 不稳定，可将 `link.lds` 恢复到单一 `All PT_LOAD`，保留 proposal/tasks 中的失败记录，再单独调查 loader 边界问题。

## Open Questions

- 当前 change 无阻塞性开放问题。
