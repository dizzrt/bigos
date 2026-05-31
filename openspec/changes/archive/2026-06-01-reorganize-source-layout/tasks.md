## 1. 准备与基线确认

- [x] 1.1 盘点当前根目录实现源码路径、公共头文件路径、构建入口、IDE 配置、README、AGENTS 和 OpenSpec project context 中的路径引用。
- [x] 1.2 确认当前 `xmake.lua` 收集的 `.cc`、`.s` 和 startup object 输入，记录迁移前的构建输入基线。
- [x] 1.3 确认本机是否具备 `xmake`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld`、clang/clangd、Bochs 和 Python/uv 工具链；不可用项记录原因和剩余风险。
- [x] 1.4 建立目标 `src/`、`cpp/`、`include/` 和 `tools/` 目录结构规划，明确 `src/kernel`、`src/mm`、`src/drivers`、`src/arch/x86/boot`、`src/runtime`、顶层 `cpp`、`cpp/include`、`cpp/ktl`、`cpp/libsupc++` 和 `tools` 的职责边界。

## 2. 迁移低风险内核实现源码

- [x] 2.1 将 `kernel` 实现源码迁移到 `src/kernel`，保持 namespace、符号名、include 形式和初始化流程不变。
- [x] 2.2 将 `drivers` 实现源码迁移到 `src/drivers`，保持 `driver::*` namespace、VGA 行为、PIC 行为、port IO 顺序和 visible failure 行为不变。
- [x] 2.3 将 `mm` 实现源码和实现私有头迁移到 `src/mm`，保持 buddy、slab、kmalloc、vmem 的初始化顺序、对象生命周期、对齐规则和失败行为不变。
- [x] 2.4 更新 `xmake.lua` 对 `src/kernel`、`src/drivers`、`src/mm` 的源码收集和 include search path 配置，移除对应 stale top-level source roots。
- [x] 2.5 更新 `.clangd` 中与 `kernel`、`drivers`、`mm` 相关的 include path，确保 public include 不需要写入 `src/` 前缀。

## 3. 保留并整理 C++ 支撑代码

- [x] 3.1 保留顶层 `cpp/` 目录作为独立 freestanding C++ support library，明确 `cpp/include`、`cpp/ktl`、`cpp/libsupc++` 的职责说明。
- [x] 3.2 保持 `cpp/ktl` 和 `cpp/libsupc++` 实现位置不变，确认 KTL 公共 API、C++ ABI stub、`new`/`delete` 语义和 freestanding 假设不变。
- [x] 3.3 保持 `cpp/include/ktl`、`cpp/include/bits`、`cpp/include/ext` 和 `cpp/libsupc++/include` 的 include root 独立性，保持 `<ktl/...>`、`<bits/...>`、`<ext/...>` include 形式和 KTL API 不变。
- [x] 3.4 更新 `xmake.lua`、`.clangd` 和文档中的 C++ 支撑库说明，确认 `cpp/include` 与 `cpp/libsupc++/include` 是 active include roots 而不是 stale compatibility paths。

## 4. 迁移 boot 与架构相关实现

- [x] 4.1 将 `arch/x86/boot` 迁移到 `src/arch/x86/boot`，保持 MBR/DBR、long-mode transition、ELF64 加载和 boot handoff 协议不变。
- [x] 4.2 将 `arch/x86/boot/install.py` 迁移到 `tools/`，更新 boot 局部 `Makefile`、`pyrightconfig.json` 和相关路径引用，确保磁盘镜像 helper 的输入输出语义不变。
- [x] 4.3 核对 boot 相关地址假设、磁盘布局假设、ELF 文件名 `kernel`、高半区跳转地址和 `BootInfo` 布局未因路径迁移改变。
- [x] 4.4 对 boot assembly/C++ 路径运行窄范围构建或 syntax check；如果 host 工具链不可用，记录不可运行原因和 bootability 风险。

## 5. 迁移 runtime startup objects

- [x] 5.1 将 `lib/src` 迁移到 `src/runtime`，保持 `crt0.s`、`crti.s`、`crtn.s` 内容语义和输出 object 名称不变。
- [x] 5.2 更新 runtime startup object 的构建路径，确保 `crt0`、`crti`、`crtbegin`、`crtend`、`crtn` 的链接顺序与迁移前一致。
- [x] 5.3 核对 `link.lds` 中 `.init`、`.fini`、`.ctors`、`.dtors` 和其他段收集规则没有被目录迁移改变。
- [x] 5.4 记录 linker-script、ABI/layout、startup object 顺序和高半区地址假设的人工 review 结果。

## 6. 更新文档与项目规范

- [x] 6.1 更新 `README.md` 和 `README-zh.md` 的仓库结构、关键文件路径、构建说明、boot flow 和子系统说明。
- [x] 6.2 更新 `AGENTS.md` 和 `openspec/config.yaml` 的 Major directories、低层风险区域、构建与验证说明。
- [x] 6.3 批量更新 active specs、当前有效文档和 `openspec/changes/archive` 中的路径引用；归档 OpenSpec change 只做路径文本替换或迁移说明，不改变历史任务状态、验证记录和设计结论。
- [x] 6.4 检查 `.github`、顶层 `Makefile`、测试配置和辅助脚本中是否存在 stale top-level implementation path，并按职责更新或记录无需更新原因。

## 7. 验证与诊断

- [x] 7.1 运行 `xmake` 主构建；如果缺少 `x86_64-elf-*` 工具链或 host 限制导致无法完成，记录失败阶段、原因和剩余风险。
- [x] 7.2 运行 C++/assembly 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include paths、无 exceptions、无 RTTI；区分历史诊断、当前迁移引入的问题和工具链 false positive。
- [x] 7.3 对 `kernel`、`drivers`、`mm` 迁移结果做初始化顺序、IRQ/port IO 安全、内存分配阶段、对象生命周期和 failure behavior review。
- [x] 7.4 在 Bochs 和 `test/bochsrc.bxrc` 本机路径可用时运行 boot smoke test；不可用时记录缺失组件和未验证的 bootability 风险。
- [x] 7.5 若本 change 修改了 Python 文件或 Python 配置，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；不可用时记录原因。
- [x] 7.6 运行 OpenSpec 校验或状态检查，确认 `source-layout-organization` spec 与 tasks 保持一致，并记录最终验证结果。


## 验证记录

- 基线盘点：迁移前实现源码位于 `kernel`、`drivers`、`mm`、`arch/x86/boot`、`lib/src`，公共头位于 `include`、`cpp/include`、`cpp/libsupc++/include`、`drivers/include`、`mm/include`；`xmake.lua` 收集 `kernel/**.cc`、`kernel/**.s`、`cpp/**.cc`、`drivers/**.cc`、`mm/**.cc`，startup object 链接顺序为 `crt0`、`crti`、`crtbegin`、普通对象、`crtend`、`crtn`。
- 工具链盘点：`xmake`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld`、`x86_64-elf-as`、`clang`、`clang++`、`clangd`、`uv`、`python3` 可用；`bochs` 不可用。
- 目录规划与迁移结果：`src/kernel`、`src/drivers`、`src/mm`、`src/arch/x86/boot`、`src/runtime`、`tools/install.py` 已建立；顶层 `cpp/`、`cpp/include`、`cpp/ktl`、`cpp/libsupc++` 和顶层 `include/` 保持独立边界。
- 构建配置：`xmake.lua` 已改为从 `src/kernel`、`src/drivers`、`src/mm` 收集 kernel target 源码，并保留顶层 `cpp/**.cc`；boot 源码仍通过 `src/arch/x86/boot/Makefile` 独立构建，避免被误链接进 kernel target；`.clangd` 使用顶层 `include`、`cpp/include`、`cpp/libsupc++/include`，不再暴露 `src/drivers/include`、`src/mm/include` 或全局 `src/mm`。
- Runtime startup：`make -C src/runtime` 通过，生成 `lib/crt0.o`、`lib/crti.o`、`lib/crtn.o`；`xmake.lua` 中 `crt0`、`crti`、`crtbegin`、`crtend`、`crtn` 的链接顺序未改变；`link.lds` 未修改，高半区地址 `0xffffffff80000000`、`.init`、`.fini`、`.ctors`、`.dtors` 收集规则未改变。
- Boot 路径：`make -C src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot` 通过；`mbr.s` 和 `dbr_exfat.s` 仍有历史 assembler warning：`found movsd; assuming movsl was meant`；未修改 boot 地址、磁盘布局、`kernel` ELF 文件名、高半区跳转地址或 `BootInfo` 布局。
- 主构建：`xmake` 已运行，能从 `src/` 新路径编译源码；构建在 `src/kernel/irq/isr.cc` 的历史 `irq_handler`/`MAX_IRQ_NUM` 诊断处失败，未到达链接和 Bochs smoke test；该诊断与归档验证记录中的旧 `kernel/irq/isr.cc` 问题一致，不是本次路径迁移新增。
- C++/assembly 辅助检查：`x86_64-elf-g++ ... -fsyntax-only` 对 `cpp/ktl`、`cpp/libsupc++`、`src/mm`、`src/drivers`、`src/kernel/bigos`、`src/kernel/kernel.cc`、`src/kernel/irq/interrupt.cc` 通过；`clang++ --target=x86_64-elf ... -fsyntax-only` 报告历史 noexcept 声明/定义不一致（`src/mm/memory.cc`、`src/drivers/irqchip/i8259.cc`、`src/kernel/irq/interrupt.cc`），与路径迁移无关，未在本 change 中改变运行时代码语义。
- Python 验证：`uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 均通过。
- 文档与规范：`README.md`、`README-zh.md`、`AGENTS.md`、`openspec/config.yaml`、active specs 和 `openspec/changes/archive` 中的路径引用已更新；归档 change 仅做路径文本刷新，未改变历史任务状态、验证结论或设计结论。
- Bochs smoke test：未运行；原因是本机 `bochs` 缺失，且未发现 `test/bochsrc.bxrc`。剩余风险是完整 emulator bootability 未验证，但 boot-stage 二进制构建和主构建路径解析已覆盖。
- OpenSpec 校验：`openspec validate reorganize-source-layout --strict` 通过，`openspec status --change "reorganize-source-layout" --json` 显示 artifacts 完整。
