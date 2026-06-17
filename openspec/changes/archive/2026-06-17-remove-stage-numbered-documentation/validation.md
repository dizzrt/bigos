## 验证摘要

本变更只修改 Markdown 文档与 OpenSpec artifacts，不修改 `kernel/**`、`user/**`、`include/**`、`tools/**`、`xmake.lua` 或任何 runtime/helper 代码。因此 runtime build、clang/clangd、QEMU smoke 和 Bochs cross-validation 不适用。

### 保留的 `stage` 命中类别

- `boot-stage`、`first-stage`、`kernel-stage`：启动、IDT 或 boot artifact 的低层执行阶段语义，非 roadmap 编号。
- `failed stage`、`build stage`、`QEMU smoke stage`、UEFI preflight/ESP generation stage：工具错误字段或验证流水线阶段，非 roadmap 编号。
- `write-stage`、`verify-stage`：persistent `/rw` clean-sync 双启动验证结果字段，属于验证输出语义。
- `stage=<stage>`、`stage=`、`<stage>`：既有 marker 或 smoke failure 输出契约，不能改动可观察文本。
- `one-stage pipe` / `single-stage pipe`：shell pipeline 数量语义，非 roadmap 编号。
- 未带编号的 prose 用法（例如 `This stage`）：当前搜索未归类为历史 roadmap 编号；后续文档维护应优先改为 capability/behavior 名称。

### 保留的 roadmap task 编号命中类别

- `roadmap.md` 中的 `Task M1.1` 到 `Task M5.4` 是 roadmap 自身的 M1-M5 capability-first 任务清单，保留为 roadmap 内部规划结构。
- 其他 README、docs、active specs、archive 与 OpenSpec workflow 指南不得引用这些 task 编号作为 change 范围、验证分组或历史记录索引。

### 清理结果

- `Stage N`、`stage N`、`Stages N`、`stages N`、`阶段 N`、`阶段N` 历史编号已替换为 capability、behavior、implementation boundary 或 validation matrix 表述。
- active OpenSpec specs 保留 Requirement/Scenario 结构与 SHALL/MUST 语义，并将 bounded POSIX-like surface、bounded libc foundation、runtime filesystem maturity、persistent clean-sync `/rw` storage 等能力名作为事实来源。
- `docs/en/**` 与 `docs/zh/**` 文件集合保持同构，抽查的对应架构文档都已从历史编号转为能力表述。
