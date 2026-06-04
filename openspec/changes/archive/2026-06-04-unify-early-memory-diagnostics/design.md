## Context

BigOS 已进入内存与中断基础设施较完整的阶段，但早期致命失败处理散落在多个文件中，
模式不统一：

- `src/kernel/irq/interrupt.cc`：`halt_cpu()` 先 `disableIRQ()` 再 `while(true){hlt}`；
  异常/`#PF` 分别打印 `BIGOS_EXCEPTION` / `BIGOS_PAGE_FAULT` 后调用 `halt_cpu()`。
- `src/mm/buddy.cc`：`halt_memory_handoff_failed()` 打印 `"invalid boot memory map"`
  （无 `BIGOS_` 前缀），`halt_early_metadata_exhausted()` 打印 arena 用量后裸 `hlt`，
  二者均未先关中断。
- `src/mm/slab.cc` / `src/mm/kmem.cc`：`BIGOS_SLAB_DEBUG` guard 直接打印
  `"slab debug guard: ..."`（无前缀）后裸 `hlt`。
- `src/mm/self_test.cc`：`fail(stage)` 打印 `BIGOS_MM_SELF_TEST_FAILED stage=` 后裸 `hlt`。

约束：freestanding（无 libc/异常/RTTI）、单核、早期关中断、无 scheduler/SMP/用户态。
输出后端只有 VGA 文本模式和 COM1 串口。不得改动 boot 固定地址、linker 布局、
IDT/`InterruptFrame` ABI、内存分配 API 语义。

## Goals / Non-Goals

**Goals:**
- 提供单一 freestanding-safe 的致命诊断/panic 入口，统一“输出诊断 → 关中断 → 安全停机”。
- 定义稳定错误码枚举与固定 `BIGOS_PANIC` marker，使 emulator serial smoke 可识别。
- 致命诊断同时写 COM1 与 VGA。
- 将现有分散的致命停机路径迁移到统一设施，保留既有诊断 marker 的兼容输出。
- 统一所有致命 marker 带 `BIGOS_` 前缀（修正 `"invalid boot memory map"` 等）。

**Non-Goals:**
- 不实现 logging subsystem、日志分级、环形缓冲、symbolication、stack unwinding、
  持久化 crash dump。
- 不引入可恢复异常或 demand paging（`#PF` 仍诊断-only）。
- 不改变 allocator 的非致命失败返回语义（`kmalloc` 失败仍返回 `nullptr`）。
- 不引入并发/锁模型。

## Decisions

### D1: 统一入口 `bigos::kpanic(code, source, fmt, ...)` + `[[noreturn]]`

提供一个变参 panic 入口与一个稳定错误码枚举（如 `PanicCode`）。语义：
1. 输出固定首行 marker `BIGOS_PANIC code=<code> source=<source>`（先串口后 VGA）；
2. 输出调用方提供的格式化上下文行；
3. `disableIRQ()`（`cli`）后进入 `while(true){hlt}`。

入口标注 `[[noreturn]] noexcept`。提供一个轻量宏或重载用于“仅 marker + 停机”场景。

**理由**：单点收敛保证“先关中断后停机”的次序一致（修复 mm 路径漏关中断），并保证
marker 前缀一致。**备选**：保留各处 `halt_cpu()` 仅统一字符串前缀——被否，无法解决
关中断次序不一致与重复 `hlt` 模板问题。

### D2: 放置位置与命名空间

头文件 `include/bigos/panic.h`，实现 `src/kernel/bigos/panic.cc`，命名空间 `bigos`，
依赖既有 `bigos::serial_puts` / `bigos::kprintf`（`bigos/io.h`）。

**理由**：与现有 `io.cc`/`utils.cc` 同层，保持公共头最小。**备选**：放入 `bigos::mm`
——被否，panic 是跨子系统内核能力，不应锚定到 mm。

### D3: 错误码分域

`PanicCode` 按来源分段编号（mm-buddy/mm-slab/mm-vmem/mm-arena/self-test/
irq-exception/irq-pagefault/generic），数值稳定、ASCII 可打印为十六进制。boot 早期
来源段按 D7 暂不在本 change 接入，可预留编号但不连线。

**理由**：稳定错误码便于 smoke 断言与未来调试关联，分域避免冲突。**备选**：自由字符串
——保留为 `source`/`msg`，但错误码必须是枚举以保证可断言稳定性。

### D4: 现有 marker 兼容策略

迁移时**保留**既有诊断行（`BIGOS_EXCEPTION`、`BIGOS_PAGE_FAULT`、
`BIGOS_MM_SELF_TEST_FAILED stage=`），由原处先打印，再走统一停机原语；新增的
`BIGOS_PANIC` 用于此前**无前缀**的致命路径（buddy/slab guard）。self-test 的
`stage=` 输出契约不变。

**理由**：避免破坏既有 smoke 断言与 spec 中已定义的可观察行为。**备选**：把所有路径
统一改成 `BIGOS_PANIC` 单一 marker——被否，会改变 interrupt/self-test 既有 spec 的
可观察 marker，超出本 change 的非破坏性范围。

### D5: 可选诊断快照

panic 入口提供可选 hook 形式（参数或单独 `kpanic_with_mm_stats`），在致命时附带
allocator 统计（复用 `print_slab_stats()`、early arena 用量）。默认路径不强制调用，
保持 freestanding 与最小副作用；快照本身不得再分配内存或触发可能失败的路径。

**理由**：诊断信息有价值但不能成为 panic 的新失败源。**备选**：panic 内强制 dump
——被否，可能在内存已损坏时再次触发故障。

### D6: `BIGOS_PANIC` 首行采用键=值空格分隔风格

`BIGOS_PANIC` 首行 marker MUST 与既有 `BIGOS_EXCEPTION` 行保持一致的 `键=值` 空格分隔
风格，固定格式为 `BIGOS_PANIC code=<code> source=<source>`，其中 `code` 以十六进制
打印、`source` 为稳定来源标识字符串；变参上下文另起后续行输出。

**理由**：统一解析格式让 serial-marker smoke 与未来日志工具复用同一套断言/解析逻辑，
避免每个 marker 各自风格。**备选**：自定义紧凑格式（如冒号分隔或单行拼接全部字段）
——被否，与现有 `BIGOS_EXCEPTION`/`BIGOS_PAGE_FAULT` 风格不一致，增加解析负担。

### D7: 本 change 不接入 boot 早期路径

统一 panic 设施的接入范围限定为 kernel 运行时与 mm/irq 路径。boot 早期代码
（含 `src/arch/x86/boot/boot.cc`、`boot.s` 等长模式切换前后路径）**不**在本 change
纳入，保留其现有失败/停机方式。

**理由**：boot 早期处于不同地址空间与运行时环境（无 higher-half kernel 上下文、
输出后端与 BootInfo handoff 假设不同），强行复用 kernel 运行时 panic 会引入跨地址
依赖风险；该路径应留待 UEFI/boot 专项统一处理。**备选**：本 change 一并接入 boot
——被否，超出“kernel 运行时 + mm/irq 诊断收敛”的聚焦范围，且与 boot 专项重叠。

## Risks / Trade-offs

- [统一停机改变了 mm 路径的关中断时机（之前未 `cli`）] → 这是修复而非回归；mm 致命
  路径本就应在停机前关中断，新行为更安全，且不影响成功路径。
- [变参 `kprintf` 在内存严重损坏时可能不可靠] → 首行固定 marker 用 `serial_puts`
  常量字符串先输出，保证最关键信号在格式化之前落地。
- [marker 字符串变更破坏 smoke 断言] → 通过 D4 保留既有 marker，仅为无前缀路径新增
  `BIGOS_PANIC`；在 tasks 中显式核对 `boot_debug.py` 与 tests 的断言字符串。
- [新增公共头增加耦合] → 头文件仅暴露 `kpanic` 与 `PanicCode`，include 最小化。
- [freestanding 误用] → 实现不使用 heap/异常/RTTI；clang/clangd 辅助检查按交叉构建
  flag 配置，记录无法对齐处。

## Migration Plan

1. 新增 `include/bigos/panic.h` + `src/kernel/bigos/panic.cc`，实现 `kpanic` 与错误码。
2. 迁移 IRQ：`halt_cpu()` 改为调用统一停机原语；异常/`#PF` 诊断行保留在原处。
3. 迁移 mm：buddy 两处 halt、slab/kmem guard halt、self-test `fail` 改用统一设施；
   为 buddy/slab guard 路径补 `BIGOS_PANIC` 前缀与错误码。
4. 全仓核对裸 `while(true){hlt}` 致命模板是否已收敛；boot 早期路径
   （`src/arch/x86/boot/*`）按 D7 不纳入，保留现状，`kernel()` idle 循环为正常停机
   非致命路径不动。
5. 验证：xmake 交叉构建；`page_fault_smoke` 触发后 Bochs 观察停机 marker；
   `mm_self_test` 成功/失败 marker 不变；clang/clangd 辅助检查；更新/核对 tests。

回滚：本 change 为可观察行为兼容的内部收敛，回滚即还原各文件原有 halt 片段；无持久
状态或 ABI 变更需回滚。

## Open Questions

- 无。此前两个开放问题已收敛为决策：marker 首行风格见 D6，boot 早期接入范围见 D7。
