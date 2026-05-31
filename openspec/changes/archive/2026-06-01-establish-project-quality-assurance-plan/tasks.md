## 1. 项目级规则

- [x] 1.1 更新 `openspec/config.yaml` 的 tasks 规则，要求后续 change 按影响范围声明构建、静态检查、emulator smoke test 或 targeted low-level test。
- [x] 1.2 在 tasks 规则中加入 C++ 辅助静态检查门禁：涉及 C++ 相关代码新增或改动时必须包含 clang 和 clangd 辅助检查任务，或记录不可用/不适用原因。
- [x] 1.3 在 tasks 规则中要求 C++ change 修复当前 change 引入的 clang/clangd error，以及确认有效的新增 warning。
- [x] 1.4 在 tasks 规则中要求 C++ 辅助检查尽量使用与 GCC 交叉构建一致或接近的 freestanding C++17、target、include path、禁用 hosted runtime 假设、禁用 exceptions 和禁用 RTTI 等项目兼容参数。
- [x] 1.5 在 tasks 规则中明确 clang/clangd 不能替代 `xmake` 和 `x86_64-elf-gcc`/`x86_64-elf-g++`，并保留 GCC 交叉构建作为 runtime C++ 改动的权威构建验证路径。

## 2. 低层内核审查规则

- [x] 2.1 为 boot、linker、handoff、页表和地址布局相关 change 增加 bootability、ABI/layout 和地址假设审查要求。
- [x] 2.2 为 buddy、slab、`kmalloc`、virtual memory 和 early memory initialization 相关 change 增加初始化顺序、分配阶段、对象生命周期、对齐和失败行为审查要求。
- [x] 2.3 为 IRQ、PIC、port IO、MMIO 和 driver state 相关 change 增加中断安全、可重入性、硬件访问顺序和可见失败行为审查要求。
- [x] 2.4 要求验证记录区分已通过检查、无法执行的检查、历史诊断和当前 change 引入的问题。

## 3. 规范与清理

- [x] 3.1 确认 `project-quality-assurance` spec 覆盖项目级验证、C++ clang/clangd 辅助检查、GCC 交叉构建权威验证、低层审查和工具不可用记录。
- [x] 3.2 确认 proposal、design、spec 和 `openspec/config.yaml` 使用一致的适用范围与术语。
- [x] 3.3 确认旧的 `openspec/changes/require-clang-checks-for-cpp-changes` change 不再保留，避免重复或冲突的 OpenSpec proposal。

## 4. 验证

- [x] 4.1 运行 `openspec status --change "establish-project-quality-assurance-plan"`，确认 change artifact 状态正常。
- [x] 4.2 运行项目可用的 OpenSpec 校验命令，或记录当前 OpenSpec CLI 未提供对应校验命令的情况。
- [x] 4.3 手动检查后续 C++ change 的 tasks 生成指令会包含 clang/clangd 辅助检查、有效新增诊断修复、GCC 交叉构建权威验证和工具不可用记录要求。
- [x] 4.4 记录本 change 只修改 OpenSpec 文档和配置，不修改 C++ 源码、头文件或 C++ 构建配置，因此不需要执行 clang/clangd 代码检查。

## 验证记录

- 已通过：`openspec status --change "establish-project-quality-assurance-plan"`，4/4 artifacts complete。
- 已通过：`openspec validate "establish-project-quality-assurance-plan" --strict --no-interactive`。
- 已确认：`openspec/changes/require-clang-checks-for-cpp-changes` 不存在，未保留重复或冲突的 OpenSpec proposal。
- 不适用：本 change 只修改 OpenSpec 文档和 `openspec/config.yaml`，不修改 C++ 源码、头文件或 C++ 构建配置，因此未运行 clang/clangd 代码检查。
