## Purpose

Define the project-level quality assurance requirements for BigOS OpenSpec changes, including impact-scoped validation, C++ clang/clangd auxiliary checks, authoritative GCC cross-toolchain builds, low-level kernel review points, and explicit records for unavailable validation.
## Requirements
### Requirement: Change 必须按影响范围声明质量验证

每个 OpenSpec change 必须（MUST）包含与其影响的子系统、语言和构建产物匹配的验证任务。验证任务必须（MUST）足够具体，能够判断该 change 已执行检查、因明确原因跳过检查，或被不可用工具阻塞。

#### Scenario: Change 影响内核运行时代码

- **WHEN** OpenSpec change 修改 `kernel/core`、`kernel/mm`、`kernel/drivers`、`cpp`、`include`、`kernel/arch` 或 `kernel/runtime` 下的内核运行时代码
- **THEN** 其 tasks 必须（MUST）包含适用于受影响区域的构建、静态检查、emulator smoke test 或 targeted low-level test

#### Scenario: Change 仅修改文档

- **WHEN** OpenSpec change 只修改文档或 OpenSpec artifacts
- **THEN** 其 tasks 必须（MUST）说明不需要 runtime build 或 emulator validation，并包含适当的文档或 OpenSpec 一致性检查

### Requirement: C++ change 必须运行 clang/clangd 辅助检查

每个新增或修改 C++ 相关代码的 OpenSpec change 必须（MUST）包含 clang 和 clangd 相关辅助静态检查任务，或记录不可用/不适用原因。C++ 相关代码包括 C++ 源文件、C++ 头文件、内核 C++ 支撑代码、KTL 代码，以及影响 C++ 编译或 clangd 解析的构建配置。

#### Scenario: Change 新增或修改 C++ 源码或头文件

- **WHEN** OpenSpec change 新增或修改 C++ 源文件或头文件
- **THEN** 其 tasks 必须（MUST）包含针对受影响 C++ 代码的 clang 和 clangd 辅助检查步骤，或记录不可用/不适用原因

#### Scenario: Change 修改 C++ 构建配置

- **WHEN** OpenSpec change 修改会影响 C++ compile flags、include paths、target triples、generated headers 或 compile database 行为的构建配置
- **THEN** 其 tasks 必须（MUST）包含针对受影响 C++ 配置的 clang 和 clangd 辅助检查步骤，或记录不可用/不适用原因

### Requirement: C++ change 必须修复引入的有效 clang/clangd 诊断

C++ 相关 OpenSpec change 必须（MUST）在标记相关实现任务完成前，修复当前 change 引入的 clang/clangd error 和确认有效的新增 warning。历史诊断可以保留，但验证记录必须（MUST）区分历史诊断、当前 change 引入的诊断，以及由 clang 与 GCC 或 freestanding 配置差异导致的误报。

#### Scenario: clang 报告当前 change 引入的 error

- **WHEN** clang 报告由当前 C++ 相关 change 引入的 error
- **THEN** 该 change 必须（MUST）修复这些 error 后才能标记相关实现任务完成

#### Scenario: clangd 报告当前 change 引入的 error

- **WHEN** clangd 报告由当前 C++ 相关 change 引入的 error
- **THEN** 该 change 必须（MUST）修复这些 error 后才能标记相关实现任务完成

#### Scenario: clang 或 clangd 报告确认有效的新增 warning

- **WHEN** clang 或 clangd 报告由当前 C++ 相关 change 引入且确认有效的 warning
- **THEN** 该 change 必须（MUST）修复这些 warning，或记录无法修复的明确原因和剩余风险

#### Scenario: clang 或 clangd 报告历史诊断或误报

- **WHEN** clang 或 clangd 报告的诊断早于当前 change，或被确认为 clang 与 GCC、freestanding 配置、compile database 差异导致的误报
- **THEN** 验证记录必须（MUST）区分该诊断不是当前 change 引入的有效问题，并记录判断依据

### Requirement: clang/clangd 检查必须保持辅助定位

clang/clangd 检查必须（MUST）被定位为辅助静态检查信号，不能替代 BigOS 当前基于 `x86_64-elf-gcc`/`x86_64-elf-g++` 和 `xmake` 的权威构建验证。

#### Scenario: clang/clangd 检查通过

- **WHEN** C++ 相关 change 的 clang 和 clangd 辅助检查通过
- **THEN** 该结果不得（MUST NOT）被用来替代受影响 runtime C++ 代码所需的 GCC 交叉构建验证

#### Scenario: clang/clangd 诊断与 GCC 构建结果不一致

- **WHEN** clang/clangd 诊断与 `x86_64-elf-gcc`/`x86_64-elf-g++` 构建结果不一致
- **THEN** 验证记录必须（MUST）说明差异、采用的判断依据和剩余风险

### Requirement: C++ 辅助检查必须尽量匹配 freestanding kernel 假设

C++ 辅助检查任务必须（MUST）使用或明确记录无法使用与 BigOS freestanding C++17 kernel 环境匹配的 compiler/clangd 设置，包括目标架构、include paths、禁用 hosted runtime 假设、禁用 exceptions、禁用 RTTI 等适用配置。

#### Scenario: 配置 clang 辅助检查

- **WHEN** OpenSpec change 为 C++ 代码定义 clang 辅助检查
- **THEN** 该检查必须（MUST）使用 freestanding C++17 kernel 兼容 flags，或记录等价 flags 不可用的原因

#### Scenario: 配置 clangd 辅助检查

- **WHEN** OpenSpec change 为 C++ 代码定义 clangd 辅助检查
- **THEN** 该检查必须（MUST）使用 compile database 或等价 compile flags，使受影响 C++ 文件尽量接近真实 GCC 交叉构建配置

### Requirement: GCC 交叉构建必须作为 C++ runtime change 的权威验证

C++ 质量验证不得（MUST NOT）用 clang/clangd 结果替代预期的 `xmake` 与 `x86_64-elf-gcc`/`x86_64-elf-g++` 构建路径。影响 compiled kernel、bootloader、C++ 支撑库、内存管理、IRQ、driver 或 public header runtime 行为的 change，必须（MUST）包含最窄可行的 GCC 交叉构建验证，或记录该验证无法运行的原因。

#### Scenario: Change 修改 C++ runtime 代码

- **WHEN** OpenSpec change 修改 compiled C++ runtime code 或 runtime code 使用的 public headers
- **THEN** 其 tasks 必须（MUST）包含 `xmake`、targeted GCC cross-toolchain build 或文档化的等价验证步骤

#### Scenario: GCC 交叉构建不可用

- **WHEN** 预期的 GCC 交叉构建无法在当前环境运行
- **THEN** 验证记录必须（MUST）说明不可用的工具、阻塞原因和剩余风险

### Requirement: 低层内核 change 必须包含风险导向审查点

影响低层内核行为的 OpenSpec change 必须（MUST）包含相关风险区域的审查任务：bootability、初始化顺序、ABI 和布局、未定义行为、内存分配阶段、中断安全和硬件访问语义。

#### Scenario: Change 影响 boot 或 handoff

- **WHEN** OpenSpec change 修改 boot code、linker assumptions、handoff structures、page-table setup 或早期物理/虚拟地址布局
- **THEN** 其 tasks 必须（MUST）包含 bootability、ABI/layout 兼容性和地址布局假设审查

#### Scenario: Change 影响内存管理

- **WHEN** OpenSpec change 修改 buddy allocation、slab allocation、`kmalloc`、virtual memory 或 early memory initialization
- **THEN** 其 tasks 必须（MUST）包含初始化顺序、分配阶段、对象生命周期、对齐和失败行为审查

#### Scenario: Change 影响 IRQ 或 drivers

- **WHEN** OpenSpec change 修改 interrupt handling、PIC behavior、port IO、MMIO 或 hardware driver state
- **THEN** 其 tasks 必须（MUST）包含中断安全、可重入性、硬件访问顺序和可见失败行为审查

### Requirement: Emulator smoke 优先级必须按场景声明

BigOS 的项目级 Agent 指南 SHALL 为 QEMU 与 Bochs 的 smoke/debug 使用场景定义明确优先级，避免将任一 emulator 描述为所有调试场景的唯一默认选择。指南 SHALL 使用当前受支持的 xmake run target、display 参数形态和 `tools.bigosdev` helper 路径，不得继续把 `bochs-sdl2`、`tools/boot_debug.py` 或 `tools/install.py` 描述为稳定入口。

#### Scenario: Agent 执行自动化 smoke 或串口 marker 验证

- **WHEN** Agent 需要执行自动化 smoke test、串口 marker 检查或 CI-like 本地验证
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu -- --display none` 对应的 QEMU headless display helper 路径，或直接使用 `uv run python -m tools.bigosdev run --emulator qemu --display none` 等价参数
- **AND** 指南 MUST 要求在 QEMU 不可用时显式记录缺失依赖和剩余风险

#### Scenario: Agent 执行快速本地启动验证

- **WHEN** Agent 需要执行普通快速本地 boot validation 且不需要 Bochs 交互调试
- **THEN** 项目级指南 MUST 要求优先使用 `xmake run qemu` 的图形 display 模式或等价 QEMU helper 路径

#### Scenario: Agent 排查早期启动或硬件行为差异

- **WHEN** Agent 修改或排查 boot、linker、memory initialization、IRQ、timer、syscall、ATA PIO、port IO 或低层 driver 行为
- **THEN** 项目级指南 MUST 要求在环境支持时使用 Bochs 复核或进行 Bochs/QEMU 双 emulator 交叉验证
- **AND** 指南 MUST 保留 `xmake run bochs`、`xmake run bochs -- --display sdl2` 和 `xmake run bochs -- --display none` 作为受支持的 Bochs 调试入口

#### Scenario: Emulator 验证不可用

- **WHEN** QEMU、Bochs、cross-binutils、display/ROM 依赖或本地 emulator 配置不可用
- **THEN** 项目级指南 MUST 要求 Agent 明确记录缺失工具、无法执行的验证步骤、已执行的替代检查和剩余风险

### Requirement: 不可用验证必须显式记录

如果必需验证步骤因本地工具、emulator 配置、disk image paths、compile database 生成或项目设置不可用而无法执行，该 change 必须（MUST）在 ready for review 前记录跳过的验证、无法运行的原因和剩余风险。

#### Scenario: 工具不可用

- **WHEN** 必需验证步骤因工具缺失或配置错误而无法运行
- **THEN** 验证记录必须（MUST）说明缺失或配置错误的工具，并描述剩余风险

#### Scenario: Emulator smoke test 不可用

- **WHEN** 必需 emulator smoke test 因 QEMU、Bochs、disk image 配置、display/ROM 配置或本地 emulator 环境不可用而无法运行
- **THEN** 验证记录必须（MUST）说明缺失的 emulator 设置和已执行的替代验证

### Requirement: 文档-only change 必须验证历史 stage 编号清理
仅修改文档或 OpenSpec artifacts 的 change 如果清理历史 stage 编号叙述，SHALL 包含 targeted search 验证任务。验证 MUST 区分历史 roadmap stage 编号残留与允许保留的非 roadmap `stage` 词义。

#### Scenario: Change 清理 active 文档中的历史编号
- **WHEN** OpenSpec change 修改 `README.md`、`README-zh.md`、`roadmap.md`、`AGENTS.md`、`docs/**` 或 `openspec/specs/**` 中的 stage 相关表述
- **THEN** 其 tasks MUST 包含针对 `Stage N`、`stage N`、`阶段 N` 和 `阶段N` 的 targeted search
- **AND** 验证记录 MUST 说明残留命中属于已清理、允许保留或不适用

#### Scenario: Change 不修改 runtime code
- **WHEN** OpenSpec change 仅修改文档和 OpenSpec artifacts
- **THEN** 其 tasks MUST 明确 runtime build、clang/clangd、QEMU smoke 和 Bochs cross-validation 不适用
- **AND** 其验证 MUST 优先使用 OpenSpec 校验、文档一致性搜索和中英文镜像检查

#### Scenario: Archive 历史记录清理 stage 编号
- **WHEN** targeted search 在 `openspec/changes/archive/**` 中发现历史 roadmap stage 编号
- **THEN** change MUST 将该命中替换为能力、模块、实现边界或验证语义
- **AND** archive MUST 继续保留已归档 change 的历史事实、设计取舍、验证结论和时间顺序
- **AND** 验证记录 MUST 只把 boot-stage、failed stage、write-stage、verify-stage 或 marker `stage=<stage>` 等非 roadmap 词义归类为允许保留

### Requirement: OpenSpec change 生命周期不得引用 roadmap 编号
项目级 OpenSpec 工作流 SHALL 要求后续新增 change、归档 change 和相关文档更新使用描述性 change 名、capability 名或行为名。新建和归档 artifact MUST NOT 包含 roadmap 中的 stage 编号、roadmap task 编号，或把 roadmap 编号映射为 change 范围、任务分组、归档说明或验证分组。

#### Scenario: 新建 change artifact
- **WHEN** Agent 或开发者创建 OpenSpec change 的 proposal、design、tasks 或 specs
- **THEN** artifact MUST NOT 使用 roadmap stage 编号或 roadmap task 编号命名 change、描述范围、组织任务或说明验证目标
- **AND** artifact MAY 使用 OpenSpec artifact-local checklist 编号组织执行顺序，只要不声称这些编号来自 roadmap

#### Scenario: 归档 change artifact
- **WHEN** Agent 或开发者归档已完成的 OpenSpec change
- **THEN** archive 输出、validation notes、spec 合并说明和相关文档更新 MUST NOT 新增 roadmap stage 编号或 roadmap task 编号
- **AND** archive 内容 MUST 使用 capability、behavior 或 implementation boundary 描述完成内容

#### Scenario: 文档引用 roadmap
- **WHEN** README、AGENTS、docs 或 OpenSpec artifacts 需要引用 roadmap 中的后续方向
- **THEN** 文档 MUST 引用里程碑名称、能力名、任务描述或功能目标
- **AND** 文档 MUST NOT 引用 roadmap stage 编号、roadmap task 编号或维护编号到改造项的对应关系

#### Scenario: 质量检查发现 roadmap 编号
- **WHEN** targeted search 在新建 change、归档 change 或相关文档更新中发现 roadmap stage/task 编号引用
- **THEN** change MUST 在标记完成前将其替换为能力/行为表述，或记录该命中不是 roadmap 编号的明确依据

### Requirement: Python tooling changes must validate package entry points
OpenSpec changes that modify `tools/bigosdev/**` SHALL include Python package entry, parser, static analysis, and targeted unit validation tasks.

#### Scenario: Change modifies bigosdev Python modules
- **WHEN** a change modifies `tools/bigosdev/**`
- **THEN** its tasks MUST include `uv run python -m tools.bigosdev --help` or an equivalent package-entry parser check
- **AND** its tasks MUST include targeted Python tests, `uv run ruff check`, `uv run ruff format --check`, and `uv run pyright` when those tools are available

