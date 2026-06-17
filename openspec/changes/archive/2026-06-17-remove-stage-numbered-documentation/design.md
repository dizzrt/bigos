## Context

BigOS 的后续路线已经从历史 stage 序号推进方式转为 capability-first 规划方式。`roadmap.md` 只保留 M1-M5 的里程碑与任务，不再维护 stage 编号到具体改造项的索引表，也不应把 roadmap 中的 task 编号扩散为 OpenSpec change 或文档标识。

当前文档仍存在三类 `stage` 表述：

1. 历史路线图编号：例如旧英文或中文路线图编号以及 roadmap task 编号引用。它们应被能力名或行为名替代。
2. 低层实现词义：例如 first-stage boot code、boot-stage page tables、kernel-stage `lidt`。这些描述实际启动/执行阶段，不属于 roadmap 编号，应保留。
3. 验证/输出契约词义：例如 failed stage、write-stage result、`BIGOS_MM_SELF_TEST_FAILED stage=<stage>`。这些是工具字段或串口 marker 契约，应保留。

本变更是文档和 OpenSpec artifact 清理，不穿越 boot、memory、IRQ、driver 或 runtime 控制流边界。

## Goals / Non-Goals

**Goals:**

- 将当前文档和 active OpenSpec specs 中的历史 roadmap stage 编号替换为功能、能力、实现边界或验证矩阵名称。
- 为后续 OpenSpec change 新建、归档和文档维护建立规则：当前能力状态和规划不得依赖 roadmap stage/task 编号。
- 保持 `docs/en` 与 `docs/zh` 同步，中文镜像不残留英文历史 stage 编号表述。
- 用 targeted search 和人工分类记录验证清理结果。

**Non-Goals:**

- 不修改内核、用户态、构建系统、helper 脚本、OpenSpec CLI 或 emulator 行为。
- 不改变 boot/handoff 地址、页表布局、linker script、IDT/syscall vector、disk layout、CR3 切换或 ABI。
- 不改变 OpenSpec archive 承担历史记录的角色；本变更只把 archive 中的旧 roadmap stage/task 编号改为能力、模块、实现边界或验证语义，不改写已完成 change 的事实结论。
- 不移除实现词义或工具字段中的 `stage`，例如 boot-stage、first-stage、failed stage、write-stage、verify-stage 和 marker `stage=<stage>`。
- 不重新引入 stage 或 task 编号到 roadmap/change/documentation 的对应关系表。

## Decisions

1. **以能力名替代历史编号。**

   将旧 bounded POSIX surface、libc、runtime filesystem 和 persistent writable storage 编号表述分别改为 `bounded POSIX-like surface`、`bounded libc foundation`、`runtime filesystem maturity` 和 `persistent clean-sync /rw storage`。

   理由：能力名与当前 roadmap 的事实来源一致，且不会要求读者知道旧 stage 映射。备选方案是保留括号中的旧 stage 编号，但这会继续制造第二套规划索引，不采用。

2. **保留非 roadmap 词义的 stage。**

   `stage` 在 boot、validation pipeline 和 marker 中有实际技术含义。清理规则只针对历史路线图编号，不把所有 `stage` 字符串视为错误。

   理由：一刀切删除会破坏低层文档准确性，并可能误改既有可观察输出契约。备选方案是全仓库禁用 `stage` 字符串，但无法覆盖 boot-stage 和 failed stage 这类合法用法，不采用。

3. **active 文档、active specs 与 archive 历史记录都清理历史编号。**

   apply 阶段覆盖 `README.md`、`README-zh.md`、`roadmap.md`、`AGENTS.md`、`docs/**`、`openspec/specs/**` 和 `openspec/changes/archive/**`。archive 仍是历史记录，但历史记录应记录能力、模块、实现边界、验证语义和当时的决策事实，而不是继续保存旧 roadmap stage/task 编号索引。

   理由：archive 是历史决策记录，不是历史编号索引；保留 stage 编号会让后续读者继续依赖已废弃的规划坐标。清理 archive 的边界是替换编号表达，不改变已归档 change 的完成状态、设计取舍、验证结论或时间顺序。备选方案是保留 archive stage 编号并只在验证记录中分类，但这会留下第二套不可维护索引，不采用。

4. **新增和归档 change 都不得携带 roadmap 编号。**

   后续 `openspec/changes/<name>/proposal.md`、`design.md`、`tasks.md`、delta specs、archive validation notes 和相关文档更新，都不得用 roadmap stage/task 编号标识变更、任务或能力。允许使用 OpenSpec artifact 自身的任务编号（例如 `1.1`、`2.3`）组织执行清单，但这些编号不得引用或映射到 roadmap task 编号。

   理由：OpenSpec change 应以能力或行为命名，archive 后仍作为当前项目记录的一部分；如果归档记录继续携带 roadmap 编号，会重新建立不受维护的编号索引。备选方案是只约束新建 change、不约束 archive，但 archive 过程常同步 validation 和文档记录，仍可能把编号扩散到当前文档，不采用。

5. **文档-only 验证不运行 runtime build。**

   本变更不触及源码、构建或 helper，验证以 OpenSpec 校验、targeted search 和 docs 镜像一致性检查为主。

   理由：runtime build 和 emulator smoke 无法验证文档措辞清理，反而会引入无关工具链风险。

## Risks / Trade-offs

- [Risk] 合法 `stage` 用法被误判为残留历史编号。→ Mitigation: 清理前后都按类别记录命中，保留 boot-stage、failed stage、marker `stage=<stage>` 等允许项。
- [Risk] 中文文档仍残留英文 `Stage N` 或“阶段 N”历史编号。→ Mitigation: 对 `docs/zh` 和中文 OpenSpec specs 单独执行 targeted search。
- [Risk] 清理 archive 历史记录时误改已归档 change 的事实含义。→ Mitigation: 只把 stage/task 编号替换为能力、模块、实现边界或验证语义，不改完成状态、设计取舍、验证结论或时间顺序。
- [Risk] roadmap 仍被误读为包含 stage/task 对应关系。→ Mitigation: roadmap 只描述 capability baseline、M1-M5 里程碑和任务；OpenSpec change 和文档引用能力名，不引用 roadmap 编号。
- [Risk] OpenSpec 自身任务编号与 roadmap task 编号混淆。→ Mitigation: 允许 `tasks.md` 内部执行编号，但禁止把这些编号声明为 roadmap task/stage 编号或建立映射关系。

## Migration Plan

1. 建立允许/禁止的 `stage` 分类清单。
2. 建立 roadmap task 编号引用的禁止清单，区分 OpenSpec `tasks.md` 内部执行编号和 roadmap task 编号。
3. 清理顶层文档和 docs 中的历史 stage/task 编号表述。
4. 清理 active OpenSpec specs 中的历史 stage/task 编号 requirement、scenario 和描述文本。
5. 清理 `openspec/changes/archive/**` 中的历史 stage/task 编号表述，替换为能力、模块、实现边界或验证语义。
6. 运行 `openspec validate remove-stage-numbered-documentation --strict`。
7. 运行 targeted search，确认 active 文档、active specs 和 archive 中只剩允许的 `stage` 词义，且没有 roadmap task 编号引用。

回滚策略：本变更仅修改 Markdown artifact，可通过 revert 本 change 触及的文档文件恢复。

## Open Questions

- 无。`openspec/changes/archive/**` 在本 change 中清理历史 roadmap stage/task 编号表述；archive 继续承担历史记录角色，但记录方式改为能力、模块、实现边界、验证语义和当时的决策事实。
