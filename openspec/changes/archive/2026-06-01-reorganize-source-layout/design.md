## Context

BigOS 当前把多个实现子系统直接放在仓库根目录，包括 `arch/x86/boot`、`kernel`、`mm`、`drivers`、`cpp` 和 `lib/src`。这种布局在项目早期直观，但随着 boot、内存管理、IRQ、驱动和 KTL 继续扩展，根目录会持续承载越来越多实现细节，导致项目入口配置、文档、测试、规范和实现源码的边界不清晰。

本次变更是组织结构重构，不改变内核运行时行为。受影响的子系统覆盖 boot、kernel、memory、drivers 和 startup objects；顶层 `cpp/` 作为独立 freestanding C++ 支撑库保留，但需要同步梳理其边界、构建路径和文档说明。

当前关键约束：

- `xmake.lua` 使用 `kernel/**.cc`、`kernel/**.s`、`cpp/**.cc`、`drivers/**.cc`、`mm/**.cc` 收集内核对象。
- `.clangd` 使用 `-Iinclude`、`-Icpp/include`、`-Icpp/libsupc++/include`、`-Idrivers/include`、`-Imm`、`-Imm/include` 提供编辑器诊断。
- `link.lds` 决定高半区内核布局，不能因目录迁移改变段布局、入口符号、构造析构段收集或 startup object 顺序。
- `arch/x86/boot` 同时包含 boot assembly、boot C++、MBR/DBR 和磁盘安装辅助脚本，迁移时需要单独验证路径和工具链；其中磁盘安装辅助脚本会按工具职责迁移到顶层 `tools/`。

## Goals / Non-Goals

**Goals:**

- 将实现源码统一收敛到 `kernel/` 下，使仓库根目录更像项目入口而不是源码集合。
- 保留顶层 `cpp/` 作为独立 freestanding C++ 支撑库目录，继续承载 KTL、C++ ABI stub、`new`/`delete` 和专属 include root。
- 保持 `include/` 作为顶层内核公共头文件目录，继续承载公共内核 API、freestanding C header subset 和跨子系统接口。
- 将 boot 磁盘安装辅助脚本迁移到顶层 `tools/`，使工具脚本与 boot runtime 源码解耦。
- 保持 `docs/`、`tests/`、`openspec/`、`.github/`、`xmake.lua`、`xmake/toolchains.lua`、`link.lds` 等非实现资产在顶层。
- 更新构建、IDE、文档和辅助配置中的路径引用，使开发者可以从新布局完成构建、静态检查和 boot smoke test。
- 将高风险目录迁移拆分为可 review 的阶段，尤其是 boot、startup object 和 linker 相关路径。

**Non-Goals:**

- 不改变 boot handoff 数据结构、磁盘布局、ELF 加载逻辑、内核链接地址或页表布局。
- 不改变 IRQ 向量、IDT/ISR 调用约定、PIC 行为、VGA/port IO 行为或任何硬件访问顺序。
- 不改变 buddy、slab、kmalloc、vmem 的初始化顺序、分配语义、对齐规则或失败行为。
- 不重命名 namespace、公共 API、头文件 include 形式或内核符号。
- 不新增调度器、进程、用户态、系统调用、文件系统、TTY/console 抽象或新驱动。

## Decisions

### Decision: 使用 `kernel/` 收敛内核实现源码，并保留顶层 `cpp/` 独立性

目标布局：

```text
.
|-- kernel/
|   |-- arch/
|   |   `-- x86/
|   |       `-- boot/
|   |-- kernel/
|   |-- mm/
|   |-- drivers/
|   `-- runtime/
|-- include/
|-- cpp/
|   |-- include/
|   |   |-- ktl/
|   |   |-- bits/
|   |   `-- ext/
|   |-- ktl/
|   `-- libsupc++/
|       `-- include/
|-- tools/
|-- docs/
|-- tests/
|-- openspec/
|-- xmake.lua
|-- xmake/toolchains.lua
`-- link.lds
```

理由：

- 实现源码和项目资产分离，根目录职责更清楚。
- 顶层 `include/` 继续作为内核公共接口入口，使用者不需要理解具体实现在哪个子目录。
- 顶层 `cpp/` 独立表达“C++ 支撑库”边界，避免 KTL、`bits`、`ext` 和 libsupc++ 头文件与内核公共 API 混在同一个 include root 中。
- `kernel/runtime` 比 `kernel/lib/src` 更明确，表达 crt startup object 的用途，避免 `lib/src` 双重泛化。

替代方案：

- 保持现状：移动成本最低，但根目录复杂度会继续增长。
- 将所有内容放进 `kernel/`，包括 `include/`、`docs/`、`tests/`：目录看似统一，但会把公共接口、文档和测试误归类为实现源码。
- 将 `cpp/include/ktl`、`cpp/include/bits`、`cpp/include/ext` 合并进顶层 `include/`：include root 更少，但会弱化 C++ support library 和 kernel public API 的边界。

### Decision: 迁移分阶段执行，而不是一次性移动全部目录

迁移顺序：

1. 创建 `kernel/` 目标结构和路径约定文档。
2. 迁移较低风险的 `kernel`、`drivers`、`mm` 实现，并更新 `xmake.lua`、`.clangd` 和文档。
3. 保留顶层 `cpp/`，梳理 `cpp/include`、`cpp/ktl`、`cpp/libsupc++` 的职责边界，并更新构建、IDE 和文档中的 C++ 支撑库说明。
4. 单独迁移 `arch/x86/boot` 到 `kernel/arch/x86/boot`，并将 `arch/x86/boot/install.py` 迁移到 `tools/` 后验证 boot 局部构建、disk install helper 路径和 README 中的 boot flow。
5. 迁移 `lib/src` 到 `kernel/runtime`，确认 startup object 构建输出、link order 和 `link.lds` 段收集语义不变。

理由：

- boot、runtime 和 linker 相关目录风险最高，单独 review 可以降低不可见启动失败风险。
- `kernel`、`drivers`、`mm` 的迁移主要是路径和构建匹配，适合作为前置阶段暴露构建脚本问题。

替代方案：

- 一次性移动全部目录：实现快，但 review 和回滚粒度差。
- 只更新文档不移动代码：风险最低，但不能解决根目录源码散落的问题。

### Decision: 保留 C++ 支撑代码的独立目录和 include root

迁移后应继续支持公共 include 形式，例如 `<bigos/io.h>`、`<bigos/memory.h>`、`<irq/interrupt.h>`、`<ktl/list.h>`、`<drivers/video/vga.h>`、`<drivers/irqchip/i8259.h>` 和 `<arch/x86/boot/boot_info.h>`。

理由：

- `cpp/include/ktl`、`cpp/include/bits`、`cpp/include/ext` 和 `cpp/libsupc++/include` 实际承担 C++ 支撑库自己的公共/半公共头文件职责，保留在 `cpp/` 下能表达其库边界。
- 让 driver 公共头使用 `include/drivers`，与实现源码目录 `kernel/drivers` 保持命名一致；mm 私有头继续保留在 `kernel/mm`。
- 顶层 `include/` 专注内核公共 API 和 freestanding C header subset，`cpp/include` 专注 KTL/C++ support API，职责更清晰。
- 这次不重命名 KTL API、namespace 或 C++ 支撑库 include directive。

替代方案：

- 将所有 include 改为 `kernel/...` 或相对路径：会暴露实现路径，不利于公共 API 稳定。
- 将 `cpp/include` 合并进顶层 `include/`：include root 更少，但会把 C++ support API 混入 kernel public API。
- 将 driver、mm 的所有实现私有头也移动到顶层 `include/`：可以减少 include path，但会扩大公共接口面，不适合与本次组织重构混合。

### Decision: 构建输出和链接脚本语义保持不变

`xmake.lua` 可以改为从 `kernel/core`、`kernel/mm`、`kernel/drivers`、`kernel/arch`、`kernel/runtime` 和保留的顶层 `cpp` 收集源码，但最终输出仍为 `build/kernel`，linker script 仍为顶层 `link.lds`，startup objects 的 link order 必须保持与当前一致。

地址和 ABI 约束：

- 不改变高半区链接地址 `0xffffffff80000000`。
- 不改变 `kernel` ELF 文件名和 bootloader 查找约定。
- 不改变 `crt0`、`crti`、`crtbegin`、`crtend`、`crtn` 的链接顺序。
- 不改变 `.ctors`、`.dtors`、`.init`、`.fini` 等段收集规则。

### Decision: boot 安装辅助脚本迁移到 `tools/`，Python 验证必须覆盖

`arch/x86/boot/install.py` 属于 boot disk image helper，但它是开发/安装工具，不会进入 boot runtime 或 kernel image。本次将其迁移到顶层 `tools/`，并通过参数、路径常量或文档明确它与 `kernel/arch/x86/boot`、disk image 和 Bochs 配置的关系。

理由：

- 顶层 `tools/` 能明确区分“会进入镜像/内核的源码”和“开发者运行的辅助脚本”。
- 后续新增 image、format、install、debug 等辅助脚本时有统一入口。
- Python helper 迁移会影响 `pyrightconfig.json` 和测试/格式化路径，必须纳入 Python 验证。

替代方案：

- 随 boot 目录迁移到 `kernel/arch/x86/boot/install.py`：上下文集中，但会把开发工具混入 boot runtime 源码树。
- 暂时保留在旧路径：迁移量最小，但与“实现源码进入 `kernel/`，工具进入 `tools/`”的目标不一致。

### Decision: 批量更新归档 OpenSpec change 中的路径引用

本次迁移会批量更新 `openspec/changes/archive` 中历史 change 的路径引用，使仓库内可搜索到的路径尽量指向新布局。归档内容仍保留其历史决策语义，只调整路径文本或补充迁移说明，不重新解释当时的设计结论。

理由：

- 迁移后开发者经常通过全仓搜索路径定位子系统；归档文档若保留大量旧路径，会增加误导。
- 当前项目仍处于早期阶段，归档数量有限，批量更新成本可控。

替代方案：

- 只更新 active docs/specs：diff 更小，但旧路径会长期污染搜索结果。
- 删除或忽略归档路径：会降低历史变更的可读性。

## Risks / Trade-offs

- [Risk] 路径迁移后 `xmake.lua` 漏收某些 `.cc` 或 `.s` 文件，导致内核缺对象或链接阶段才失败。→ Mitigation: 分阶段迁移后运行 `xmake`，并检查编译对象列表或链接输入包含预期子系统。
- [Risk] `.clangd` include path 未同步，编辑器出现大量误报或隐藏真实问题。→ Mitigation: 更新 `.clangd` 后运行 clang/clangd 辅助诊断，区分历史诊断、当前变更诊断和 freestanding false positive。
- [Risk] boot 目录迁移破坏 boot 局部 `Makefile`、`install.py`、Bochs 配置或文档路径。→ Mitigation: boot 迁移独立 review，运行 boot 局部构建或至少 assembler syntax check，并在 Bochs 可用时做 smoke test。
- [Risk] startup object 路径变化破坏 link order 或 `link.lds` 对 crt object 的段收集。→ Mitigation: 保持输出 object 名称和链接顺序不变，迁移后显式核对 `crt0`、`crti`、`crtbegin`、`crtend`、`crtn` 顺序。
- [Risk] 历史 OpenSpec archive 批量更新产生较大文档 diff，可能掩盖真正的设计变化。→ Mitigation: 归档更新限制为路径文本替换或迁移说明，不改变历史 change 的任务状态、结论和验证记录。
- [Risk] `install.py` 迁移到 `tools/` 后与 boot 目录相对路径脱钩，可能破坏磁盘镜像安装流程。→ Mitigation: 将路径解析改为基于项目根或显式参数，并用 Python 检查和 boot helper dry-run/窄范围验证覆盖。
- [Risk] 移动大量文件导致 git diff 难以 review。→ Mitigation: 使用阶段性提交或至少按目录分组迁移，避免同一 patch 同时做格式化、重命名和逻辑修改。

## Migration Plan

1. 准备阶段：确认当前 `openspec status`、`xmake.lua`、`.clangd`、README 和项目指南中的旧路径引用。
2. 低风险源码阶段：迁移 `kernel`、`drivers`、`mm` 到 `kernel/`，更新构建和 include search path，运行构建/静态检查。
3. C++ 支撑阶段：保留顶层 `cpp/`、`cpp/include`、`cpp/ktl` 和 `cpp/libsupc++`，更新文档和构建说明以明确其独立 C++ support library 边界，运行 C++ 辅助检查。
4. boot/tools 阶段：迁移 `arch/x86/boot` 到 `kernel/arch/x86/boot`，迁移 `install.py` 到 `tools/`，更新 boot 局部构建、Python helper 配置和文档路径。
5. runtime 阶段：迁移 `lib/src` 到 `kernel/runtime`，确认 startup object 生成位置、link order 和 linker script 行为。
6. 文档阶段：更新 README、AGENTS、OpenSpec project context、路径说明、归档 OpenSpec change 路径引用和验证记录。
7. 验证阶段：运行 `xmake`，在工具链可用时运行 targeted compile/static checks；在 Bochs 和本机配置可用时运行 boot smoke test。

Rollback 策略：由于本次不改变运行时逻辑，回滚应以目录迁移分组为单位恢复路径和构建配置；若 boot 或 runtime 阶段验证失败，可先回滚对应阶段，同时保留已验证的 `kernel`、`drivers`、`mm` 阶段。

## Open Questions

- 无。已决策：顶层 `cpp/` 及其 `cpp/include` 作为独立 C++ 支撑库保留；`arch/x86/boot/install.py` 迁移到 `tools/`；归档 OpenSpec change 中的路径引用纳入批量更新范围。
