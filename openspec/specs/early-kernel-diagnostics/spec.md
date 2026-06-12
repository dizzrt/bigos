## Purpose

定义 BigOS 早期内核致命诊断设施，统一 panic marker、稳定错误码、关中断后安全停机行为，以及中断/异常和内存致命路径的可复现验证要求。

## Requirements

### Requirement: 统一早期内核 panic 入口

BigOS SHALL 提供单一 freestanding-safe 的早期内核致命诊断入口 `bigos::kpanic`，
该入口 MUST 标注为不返回（`[[noreturn]] noexcept`），并 MUST 在输出诊断信息后关闭可
屏蔽中断（`cli`）再进入安全停机（`hlt` 循环）。该入口 MUST NOT 依赖 heap 分配、
异常、RTTI、scheduler、IRQ 上下文服务或 hosted runtime API。

#### Scenario: panic 输出后关中断并停机

- **WHEN** 内核早期路径调用 `bigos::kpanic(code, source, ...)`
- **THEN** 入口先输出致命诊断信息
- **AND** 随后执行 `cli` 关闭可屏蔽中断
- **AND** 进入 `hlt` 停机循环且不返回调用方

#### Scenario: panic 路径保持 freestanding-safe

- **WHEN** `bigos::kpanic` 执行
- **THEN** 它 MUST NOT 调用 `kmalloc`/`alloc_kernel_pages`/`alloc_physical_order`
  或任何可能再次失败的动态分配路径
- **AND** 不依赖 scheduler、SMP、文件系统或用户态服务

### Requirement: 致命诊断输出固定 marker 与稳定错误码

BigOS 致命诊断 SHALL 输出固定的 `BIGOS_PANIC` marker，并 MUST 携带来自稳定错误码
枚举的错误码值与来源标识，使 emulator serial smoke 工具可确定性识别。致命 marker
首行 MUST 使用常量字符串先于任何变参格式化输出，保证最关键信号优先落地。该首行
MUST 采用与既有 `BIGOS_EXCEPTION` 行一致的 `键=值` 空格分隔风格，固定格式为
`BIGOS_PANIC code=<code> source=<source>`，其中 `code` 以十六进制打印、`source`
为稳定来源标识；变参上下文 MUST 另起后续行输出。

#### Scenario: panic 输出固定 marker 首行

- **WHEN** 此前无前缀的致命路径（如 buddy handoff 失败、early arena 耗尽、
  slab debug guard 触发）改为通过统一设施停机
- **THEN** 诊断输出包含固定 `BIGOS_PANIC` marker，且带 `BIGOS_` 前缀
- **AND** marker 首行采用 `BIGOS_PANIC code=<code> source=<source>` 的键=值空格分隔格式

#### Scenario: 错误码来自稳定枚举

- **WHEN** 不同子系统（mm-buddy、mm-slab、mm-arena、self-test、irq-exception、
  irq-pagefault）触发 panic
- **THEN** 每个来源使用稳定且互不冲突的错误码枚举值
- **AND** 错误码以确定性、ASCII 可打印的十六进制形式输出

### Requirement: 致命诊断同时输出到串口与 VGA

BigOS 致命诊断 SHALL 同时输出到 COM1 串口与 VGA 文本后端，复用既有
`bigos::serial_puts` 与 `bigos::kprintf`，MUST NOT 引入新的输出依赖。

#### Scenario: 双后端输出

- **WHEN** `bigos::kpanic` 输出致命诊断
- **THEN** 致命 marker 与上下文同时写入 COM1 串口与 VGA
- **AND** 在 emulator 串口日志中可观察到 `BIGOS_PANIC` marker

### Requirement: 现有致命路径收敛到统一设施且保留既有可观察 marker

BigOS SHALL 将现有分散的致命停机路径迁移到统一诊断设施，并 MUST 保持其“安全停机”
的可观察行为不变。迁移 MUST 保留既有诊断 marker（`BIGOS_EXCEPTION`、
`BIGOS_PAGE_FAULT`、`BIGOS_MM_SELF_TEST_FAILED stage=`）的输出契约。仓库内致命停机
路径 MUST NOT 继续保留各自独立、缺少关中断或缺少 `BIGOS_` 前缀的裸 `hlt` 模板。

#### Scenario: 中断/异常停机收敛

- **WHEN** CPU 异常或 `#PF` 诊断完成需要安全停机
- **THEN** 停机通过统一关中断 + 停机原语执行
- **AND** 既有 `BIGOS_EXCEPTION` 与 `BIGOS_PAGE_FAULT` 诊断输出保持不变

#### Scenario: 内存致命路径收敛并补齐前缀

- **WHEN** buddy 启动 handoff 失败、early metadata arena 耗尽，或
  `BIGOS_SLAB_DEBUG` guard 检测到非法生命周期操作
- **THEN** 这些路径通过统一 panic 设施停机，先关中断后 `hlt`
- **AND** 其致命 marker 带 `BIGOS_` 前缀与稳定错误码，不再使用无前缀字符串

#### Scenario: self-test 失败保持兼容输出

- **WHEN** 内存运行时 self-test 检测到失败并停机
- **THEN** 仍输出既有 `BIGOS_MM_SELF_TEST_FAILED stage=<stage>` 内容
- **AND** 停机经由统一关中断 + 停机原语

#### Scenario: 不残留独立致命停机模板

- **WHEN** 开发者搜索内核运行时与 `kernel/mm`、`kernel/core/irq` 的致命停机路径
- **THEN** 致命停机统一经由 `bigos::kpanic` 或其停机原语
- **AND** 不存在缺少关中断或缺少 `BIGOS_` 前缀的独立 `while(true){hlt}` 致命片段

### Requirement: 可选诊断快照不得引入新失败源

BigOS panic 设施 SHALL 提供可选的诊断快照能力，用于在致命时附带 allocator 统计
（复用既有 slab 统计与 early arena 用量）。默认 panic 路径 MUST NOT 强制执行快照，
且快照实现 MUST NOT 进行动态分配或触发可能再次失败的路径。

#### Scenario: 默认 panic 不强制快照

- **WHEN** 调用方使用不带快照的 panic 入口
- **THEN** panic 仅输出固定 marker、错误码与调用方上下文后停机
- **AND** 不调用 allocator 统计聚合

#### Scenario: 启用快照时安全输出

- **WHEN** 调用方显式请求带 allocator 统计的 panic 快照
- **THEN** 快照复用既有只读统计源输出诊断
- **AND** MUST NOT 分配内存或触发新的失败路径

### Requirement: 统一诊断设施验证可复现

BigOS SHALL 为本能力提供可复现验证。验证 MUST 至少包含项目支持的最窄交叉构建，并在
Bochs 与 boot 资产可用时执行 panic/停机 marker 的有界 emulator smoke。若 emulator 不
可用，验证记录 MUST 明确说明缺失依赖与剩余 bootability 风险。涉及 C++ 变更时 MUST 记录
clang/clangd 辅助静态检查结果或其不可用原因。

#### Scenario: 构建验证

- **WHEN** 完成统一诊断设施实现
- **THEN** 运行项目支持的最窄交叉构建并记录成功或失败原因

#### Scenario: panic 停机 smoke

- **WHEN** Bochs、ROM 路径、生成磁盘镜像与串口 marker oracle 可用，且启用了一个会触发
  停机的验证路径（如 `page_fault_smoke`）
- **THEN** 验证在有界时间内观察到致命停机 marker 并确认内核安全停机

#### Scenario: emulator 不可用时记录缺口

- **WHEN** Bochs 运行时 smoke 因本机模拟器、ROM 或 oracle 不可用而无法执行
- **THEN** 验证记录缺失依赖、已通过的替代检查与剩余 bootability 风险
