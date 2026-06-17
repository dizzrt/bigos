## ADDED Requirements

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
