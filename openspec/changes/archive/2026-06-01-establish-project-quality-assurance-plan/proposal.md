## 背景与动机

BigOS 是处于早期 bring-up 阶段的 freestanding x86_64 内核，后续变更会持续触及 boot、IRQ、内存管理、驱动、C++ 运行时支撑库和公共头文件。项目已经有 Python 检查要求，但整体质量保证计划仍不完整，尤其缺少对 GCC 交叉构建权威验证、clang/clangd 辅助静态检查、低层内核审查点和工具不可用记录的统一规范。

## 变更内容

- 新增项目级质量保证计划，要求后续 OpenSpec change 按变更类型声明适用的验证任务。
- 将原 `require-clang-checks-for-cpp-changes` 的 clang/clangd 要求整合进更完整的 C++ 质量保证规则，并明确 clang/clangd 是辅助静态检查，不替代 GCC 交叉构建：
  - 涉及 C++ 相关代码新增或改动时，必须包含 clang 和 clangd 相关辅助检查，或记录不可用/不适用原因。
  - 必须修复当前 change 引入的 clang/clangd error，以及确认有效的新增 warning。
  - 对由 clang 与 GCC、freestanding 配置或 clangd compile database 差异导致的诊断，必须记录判断依据和剩余风险。
- 要求 C++ 质量检查与当前项目事实保持一致：
  - `x86_64-elf-gcc`/`x86_64-elf-g++` 与 `xmake` 仍是当前权威构建验证路径。
  - clang/clangd 检查必须尽量使用与 GCC 交叉构建一致或接近的 freestanding、C++17、目标架构、include path 和构建配置参数。
  - 不把 hosted C++ runtime、异常、RTTI、线程、文件系统或 OS 服务假设引入内核代码。
- 要求涉及低层内核风险区域的 change 明确审查启动可用性、初始化顺序、ABI/布局、未定义行为、内存分配阶段、中断安全和硬件访问语义。
- 要求验证记录区分“已执行并通过”、“无法执行并记录原因”和“存在历史诊断但未引入新增问题”。
- 删除旧的 `openspec/changes/require-clang-checks-for-cpp-changes`，避免保留重复且范围较窄的 change。

## 能力范围

### 新增能力

- `project-quality-assurance`：定义 BigOS 后续 OpenSpec change 的项目级质量保证要求，包括 GCC 交叉构建权威验证、clang/clangd 辅助静态检查、低层内核审查点、验证记录和工具不可用处理。

### 修改能力

- 无。

## 影响范围

- 受影响子系统：项目级 OpenSpec 工作流、质量保证规则和后续 change 的 tasks 生成约束。
- 受影响代码与配置：
  - `openspec/config.yaml`
  - `openspec/specs/project-quality-assurance/spec.md`
  - 后续涉及 boot、kernel、mm、drivers、cpp、include、lib/src、arch/x86、test 或构建配置的 OpenSpec change。
- 基本假设：
  - 架构仍为 x86_64 freestanding kernel。
  - C++ 代码仍以 C++17、freestanding 模式和当前交叉工具链约束为基础。
  - `xmake` 与 `x86_64-elf-gcc`/`x86_64-elf-g++` 仍是当前权威构建验证路径。
  - Bochs 仍是本地 emulator smoke test 的默认方向，但本地配置可能缺失。
  - clang/clangd 辅助检查依赖可用的 clang 工具链、compile database 或等价编译参数。
- 非目标：
  - 不要求本 change 清理全仓库历史 clang/clangd warning，也不把 clang 作为项目权威编译器。
  - 不引入新的 hosted runtime、异常、RTTI、线程、文件/socket/environment 等 OS 服务假设。
  - 不改变 boot 地址、linker 地址、页表布局、中断向量、disk layout 或 ABI。
  - 不替换现有构建系统或交叉工具链。
