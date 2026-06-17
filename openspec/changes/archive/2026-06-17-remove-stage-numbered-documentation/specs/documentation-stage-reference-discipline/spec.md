## ADDED Requirements

### Requirement: 当前文档和历史记录不得依赖历史 stage 或 roadmap task 编号描述能力
BigOS 当前文档和 OpenSpec archive 历史记录 SHALL 使用功能、能力、模块、实现边界或验证矩阵名称描述当前状态、规划、验证要求和已归档事实。顶层 README、roadmap、Agent 指南、`docs/en`、`docs/zh`、active OpenSpec specs 和 `openspec/changes/archive/**` MUST NOT 使用历史 roadmap stage 编号或 roadmap task 编号作为当前能力、后续规划、change 范围、验证边界或历史记录索引的事实来源。

#### Scenario: 文档描述当前能力基线
- **WHEN** 当前文档描述已实现的用户态、文件系统、内存、调度、syscall 或验证能力
- **THEN** 文档 MUST 使用 bounded userland baseline、bounded POSIX-like surface、bounded libc foundation、runtime filesystem maturity、persistent clean-sync `/rw` storage、runtime smoke validation matrix 等能力名
- **AND** 文档 MUST NOT 使用 `Stage N`、`stage N`、`阶段 N`、`阶段N`、roadmap task 编号或 roadmap task 标题编号指代这些能力

#### Scenario: 文档描述后续规划
- **WHEN** 当前文档描述 roadmap 后续工作
- **THEN** 文档 MUST 使用 M1-M5 里程碑、任务名、功能目标或 capability 名称
- **AND** 文档 MUST NOT 重新引入 stage 编号或 roadmap task 编号到具体改造项的对应关系

#### Scenario: Archive 描述已归档事实
- **WHEN** `openspec/changes/archive/**` 描述已完成 change 的背景、设计、任务、验证或规格 delta
- **THEN** archive MUST 使用能力、模块、实现边界、验证语义或当时的决策事实描述历史记录
- **AND** archive MUST NOT 使用历史 roadmap stage 编号或 roadmap task 编号作为历史记录索引

### Requirement: 后续 change 与归档记录不得包含 roadmap 编号
BigOS 后续新增 OpenSpec change、归档 change 和相关文档更新 SHALL 使用能力名、行为名或 change 名描述范围。它们 MUST NOT 包含 roadmap 中的 stage 编号、roadmap task 编号或把 roadmap 编号映射到 change artifact 的描述。

#### Scenario: 新增 OpenSpec change
- **WHEN** 创建新的 OpenSpec change proposal、design、tasks 或 delta specs
- **THEN** artifact MUST 使用描述性 change 名、capability 名或行为名说明范围
- **AND** artifact MUST NOT 使用 roadmap stage 编号或 roadmap task 编号作为标题、范围说明、任务来源或验证分组

#### Scenario: 归档 OpenSpec change
- **WHEN** archive 流程生成或更新归档说明、validation notes、spec 合并说明或相关文档
- **THEN** 归档内容 MUST 保持能力/行为命名
- **AND** 归档内容 MUST NOT 新增 roadmap stage 编号或 roadmap task 编号引用

#### Scenario: OpenSpec tasks 使用内部编号
- **WHEN** `tasks.md` 使用 `1.1`、`2.3` 等内部执行编号组织 checklist
- **THEN** 这些编号 MAY 保留为 artifact-local 执行顺序
- **AND** 文档 MUST NOT 将这些编号声明为 roadmap task 编号或与 roadmap task 建立映射

### Requirement: 合法 stage 词义必须保留清晰边界
BigOS 文档 SHALL 允许非 roadmap 编号语义的 `stage` 用法。允许项 MUST 表达启动阶段、验证流程阶段、工具字段或既有输出契约，而不是历史规划索引。

#### Scenario: 文档描述 boot 或低层执行阶段
- **WHEN** 文档描述 first-stage boot code、boot-stage page tables、kernel-stage `lidt` 或同类低层执行阶段
- **THEN** 文档 MAY 保留 `stage` 词义
- **AND** 该表述 MUST NOT 暗示 roadmap stage 编号

#### Scenario: 文档描述验证字段或 marker 契约
- **WHEN** 文档描述 failed stage、write-stage result、verify-stage result 或 `BIGOS_MM_SELF_TEST_FAILED stage=<stage>` 等工具/输出契约
- **THEN** 文档 MAY 保留对应 `stage` 字段或 marker 文本
- **AND** 清理工作 MUST NOT 改变既有 marker、helper 字段或验证 oracle 的可观察文本

### Requirement: 中英文文档必须同步替换历史编号
BigOS 文档清理 SHALL 保持 `docs/en` canonical 内容与 `docs/zh` 中文镜像同步。中文文档 MUST 不残留英文 `Stage N` 或中文 `阶段 N` 历史编号表述，除非该用法属于明确允许的非 roadmap 词义。

#### Scenario: 英文架构文档替换历史编号
- **WHEN** `docs/en` 中一个历史 stage 编号被替换为能力名
- **THEN** 对应 `docs/zh` 相同相对路径文档 MUST 使用一致的中文能力表述

#### Scenario: 中文文档单独残留历史编号
- **WHEN** targeted search 在 `docs/zh` 中发现 `Stage N`、`stage N`、`阶段 N` 或 `阶段N`
- **THEN** 该命中 MUST 被替换为能力表述或记录为允许的非 roadmap 词义
