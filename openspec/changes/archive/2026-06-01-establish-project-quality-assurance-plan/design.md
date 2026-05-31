## Context

BigOS 的核心代码运行在 freestanding x86_64 kernel 环境中，质量保证不能直接套用 hosted application 的默认规则。当前仓库已经具备部分 boot、VGA、IRQ、PIC、keyboard、buddy/slab allocator 和 early virtual memory 能力，但多个子系统仍在 bring-up 阶段，任何 change 都可能影响 bootability、初始化顺序、ABI/layout、内存安全或中断安全。

现有 OpenSpec 项目规则已经包含 Python 检查要求，但 C++、assembly、构建、emulator smoke test 和低层内核审查点还没有统一的项目级计划。此前独立的 `require-clang-checks-for-cpp-changes` 只覆盖 C++ clang/clangd 检查；本 change 将其合并进完整的项目质量保证计划，并明确 clang/clangd 是辅助静态检查，GCC 交叉构建才是当前权威构建验证。

本 change 只调整 OpenSpec 规范和项目规则，不跨越 boot、memory、IRQ 或 driver 的运行时边界，不改变地址布局、页表、ABI、链接脚本、启动协议或中断向量。

## Goals / Non-Goals

**Goals:**

- 建立后续 OpenSpec change 的项目级质量保证要求。
- 要求每个 change 根据实际影响范围声明构建、静态检查、emulator smoke test 或 targeted low-level test。
- 将 C++ clang/clangd 检查作为 C++ 相关 change 的辅助静态检查门禁，并要求修复当前 change 引入的 error 与确认有效的 warning。
- 保持 `xmake` 与 `x86_64-elf-gcc`/`x86_64-elf-g++` 作为当前权威构建验证路径，明确 clang/clangd 诊断不能替代真实交叉构建。
- 明确低层内核 change 的审查维度，包括 bootability、初始化顺序、ABI/layout、未定义行为、内存分配阶段、中断安全和硬件访问语义。
- 要求无法执行的验证必须记录原因和剩余风险。

**Non-Goals:**

- 不要求本 change 完成历史 warning/error 清零。
- 不引入 hosted C++ runtime、异常、RTTI、线程、文件系统、socket 或 environment 假设。
- 不新增或替换 build system、toolchain、emulator。
- 不改变任何 kernel runtime 行为、boot protocol、disk layout、page-table layout、interrupt vector 或 ABI。

## Decisions

- 将质量保证计划建模为单一 `project-quality-assurance` capability。
  - 理由：项目级 QA 是跨语言、跨子系统的工作流约束，单一 capability 更容易作为后续 change 的统一依据。
  - 备选方案：保留独立 `cpp-clang-quality-gate` capability。该方案会让 C++ 门禁与整体 QA 规则分散，后续维护容易重复或冲突。
- 将强制规则落入 `openspec/config.yaml` 的 task rules，同时用 spec 定义可归档的项目行为。
  - 理由：task rules 会直接影响后续 artifact 生成；spec 则提供长期可审计的 requirement。
  - 备选方案：只写文档说明。该方案对后续 OpenSpec change 的约束力较弱。
- 对 C++ 使用“新增有效诊断不得进入”的增量策略。
  - 理由：BigOS 处于早期阶段，历史 clang/clangd 诊断可能需要单独清理；clang 与 GCC、freestanding 配置或 compile database 差异也可能产生误报；增量门禁的重点是修复当前 change 引入的 error 和确认有效的 warning。
  - 备选方案：立即要求全仓库零诊断。该方案会把历史技术债与功能 change 强绑定，容易阻塞开发。
- 保留 GCC 交叉构建作为权威构建检查，clang/clangd 作为辅助静态检查信号。
  - 理由：项目当前预期工具链是 `x86_64-elf-gcc`/`x86_64-elf-g++`，clang 与 GCC 对 freestanding 目标、内联汇编、ABI 细节和 warning 的行为可能不完全一致。
  - 备选方案：将 clang 作为唯一编译检查。该方案可能偏离真实构建路径。
- 工具不可用时允许记录跳过原因，而不是静默通过或强制失败。
  - 理由：本地可能缺少 Bochs、cross toolchain、compile database 或 clangd 配置；显式记录可以保留审计性和剩余风险。
  - 备选方案：所有验证不可用都阻塞 change。该方案在当前早期项目和开发机差异下可能不可操作。

## Risks / Trade-offs

- [Risk] 质量计划过宽导致每个 change 的 tasks 过重 -> Mitigation：规则要求按影响范围选择验证，非相关语言或子系统不强制执行对应检查。
- [Risk] clang/clangd 配置不稳定产生误报或与 GCC 交叉构建结论不一致 -> Mitigation：要求尽量使用与 GCC 交叉构建一致或接近的 freestanding C++17、target、include path 和构建配置参数；clang/clangd 只作为辅助静态检查，不能替代 `xmake`/`x86_64-elf-gcc` 构建验证。
- [Risk] 历史诊断与新增诊断难以区分 -> Mitigation：要求验证记录明确区分历史问题和当前 change 引入的问题。
- [Risk] 低层审查点可能被写成形式化 checklist -> Mitigation：只要求涉及相关风险区域的 change 明确审查，并要求记录具体影响或不适用原因。
- [Risk] emulator smoke test 受本机 Bochs 和 disk image 配置影响 -> Mitigation：允许记录配置缺失，同时保留构建和静态检查作为可执行验证。

## Migration Plan

- 新增 `project-quality-assurance` capability spec。
- 更新 `openspec/config.yaml` 的 tasks 规则，加入项目级 QA、GCC 交叉构建权威验证、C++ clang/clangd 辅助检查、低层内核审查和验证记录要求。
- 删除旧的 `openspec/changes/require-clang-checks-for-cpp-changes` change，避免重复 proposal。
- 本 change 不需要 kernel runtime 迁移；如规则过严或过宽，可通过后续 OpenSpec change 修改。

## Resolved Questions

- 需要后续新增 `tools/check-cpp.sh`，封装 clang、clangd、xmake 和 compile database 检查入口。
- 需要后续新增 `.clangd` 或 xmake compile database 生成约定，使 clangd 诊断更稳定。
- 暂不为 assembly、linker script 和 boot image size check 建立更细粒度的专门 capability；当前 change 仅保留通用低层内核审查要求。
