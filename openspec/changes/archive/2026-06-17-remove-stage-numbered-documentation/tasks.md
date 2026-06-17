## 1. 开发

- [x] 1.1 建立历史 roadmap stage/task 编号与允许保留 `stage` 词义的分类清单，覆盖 `Stage N`、`stage N`、`阶段 N`、`阶段N`、roadmap task 编号、boot-stage、first-stage、failed stage、write-stage、verify-stage 和 marker `stage=<stage>`。
- [x] 1.2 清理 `README.md`、`README-zh.md`、`roadmap.md`、`AGENTS.md`、`docs/README.md` 和 `docs/AGENTS.md` 中的历史 stage 编号叙述，改为当前能力、实现边界或里程碑任务表述。
- [x] 1.3 清理 `docs/en/**` 中的历史 stage 编号叙述，保留 boot-stage、first-stage、failed stage、marker 输出契约等非 roadmap 词义。
- [x] 1.4 同步清理 `docs/zh/**` 对应中文镜像，确保相同相对路径的中英文文档表达同一能力边界，中文文档不残留英文 `Stage N` 历史编号。
- [x] 1.5 清理 `openspec/specs/**` active specs 中的历史 stage 编号 requirement、scenario 和描述文本，改为 bounded POSIX-like surface、bounded libc foundation、runtime filesystem maturity、persistent clean-sync `/rw` storage 等能力名。
- [x] 1.6 清理 `openspec/changes/archive/**` 中的历史 stage/task 编号表述，改为能力、模块、实现边界或验证语义；保持 archive 的历史记录角色，不改写已归档 change 的事实结论。
- [x] 1.7 更新项目级 Agent/OpenSpec 指南，明确后续新增 change、归档 change 和相关文档更新不得引用 roadmap stage/task 编号，允许仅保留 OpenSpec artifact-local checklist 编号。

## 2. 输出

- [x] 2.1 更新后的顶层文档必须描述当前 bounded minimal usable system baseline 和 M1-M5 capability-first roadmap，不重新建立 stage/task 编号到改造项的对应关系。
- [x] 2.2 更新后的架构文档必须直接描述 subsystem capability、实现边界和验证矩阵，不再用历史 stage 编号命名当前能力。
- [x] 2.3 更新后的 active OpenSpec specs 必须表达当前规范要求，保留 OpenSpec operation header、Requirement/Scenario 结构和 SHALL/MUST 语义。
- [x] 2.4 输出一份验证摘要，列出仍保留的 `stage` 命中类别、roadmap task 编号命中类别及其保留原因。

## 3. 回归

- [x] 3.1 运行 `openspec validate remove-stage-numbered-documentation --strict`，修复新 change artifact 的格式或规格问题。
- [x] 3.2 对 `README.md`、`README-zh.md`、`AGENTS.md`、`roadmap.md`、`docs/**`、`openspec/specs/**` 和 `openspec/changes/archive/**` 运行 targeted search：`Stage N`、`stage N`、`Stages N`、`stages N`、`阶段 N`、`阶段N`、roadmap task 编号模式和 roadmap task 标题引用。
- [x] 3.3 对后续 change 模板/指南和 archive 指南执行 targeted search，确认新增与归档流程不会要求或示例化 roadmap stage/task 编号。
- [x] 3.4 对 `docs/en/**` 和 `docs/zh/**` 执行镜像一致性抽查，确认中英文对应文档都已从历史编号转为能力表述。
- [x] 3.5 记录本变更仅修改 Markdown/OpenSpec artifacts，runtime build、clang/clangd、QEMU smoke 和 Bochs cross-validation 不适用；如执行环境缺少 OpenSpec CLI 或搜索工具，明确记录 blocker 与剩余风险。
