## Context

BigOS 当前已经形成多个默认关闭的 runtime smoke：memory self-test、page fault、timer IRQ、keyboard、scheduler、user virtual memory、syscall、first user program、read-only exFAT、filesystem-backed user ELF。它们分别验证了早期内存、IRQ/timer、TTY、scheduler、syscall、filesystem 和 smoke-only user-mode path，但执行方式仍偏手工：开发者需要自己选择 `xmake f ...=y` 组合、启动 QEMU/Bochs helper、观察串口 marker，并在不可用工具场景下手动描述剩余风险。

runtime smoke validation matrix 的目标不是新增内核运行时能力，而是在进入 blocking/sleep、preemptive scheduler、process lifecycle、VFS、VMA 等更高风险阶段之前，把现有 smoke 组合收敛为一套可重复、可跳过且可审计的验证矩阵。

受影响边界主要在 tooling、validation scripts、文档和 OpenSpec 验证记录。内核 boot protocol、Legacy BIOS/MBR/exFAT raw image、higher-half kernel entry、IDT/IRQ/syscall ABI、CR3 ownership、proc smoke gating 和现有 marker 字符串都必须保持稳定。

## Goals / Non-Goals

**Goals:**

- 定义runtime smoke validation matrix runtime smoke matrix，覆盖 memory、timer、scheduler、syscall、filesystem、first user program 和 filesystem-backed user ELF 的最小有价值组合。
- 提供一个可由本地开发者或未来 CI 调用的 validation runner 或等价 helper 编排入口，复用 `xmake f`、`xmake run qemu -- --display none` 和 `tools/boot_debug.py` 的现有能力。
- 为每个 smoke case 记录 build 配置、预期 marker、串口日志路径、执行结果、跳过原因、替代检查和剩余风险。
- 将自动化默认路径固定为 QEMU headless marker 检查，同时保留 Bochs 或 QEMU+Bochs cross-validation 作为 boot/IRQ/timer/ATA PIO/port IO 高风险变更的建议。
- 保持所有 smoke 默认关闭，执行矩阵时显式配置并在 case 之间避免隐式污染后续配置。

**Non-Goals:**

- 不实现 CI 平台接入、远端 runner、dashboard 或 release gating。
- 不新增 kernel runtime feature，不改变任何 smoke marker ABI。
- 不引入 preemptive scheduler、SMP、blocking/sleep primitives、process lifecycle、VFS、VMA/demand paging、COW 或 UEFI backend。
- 不把 Bochs 作为自动化 smoke 的唯一默认路径；Bochs 仅用于本地可用时的低层交叉验证。
- 不要求单个矩阵 case 同时打开所有 smoke；组合必须保持窄，避免难以定位失败来源。

## Decisions

1. 使用独立的矩阵定义描述 smoke case，而不是把组合散落在文档示例中。

   理由：矩阵定义可以被 helper、文档和人工验证记录共同引用，避免后续阶段新增 smoke 时产生多处不一致。备选方案是只更新 README 或 AGENTS 指南，但那无法为自动执行和结构化结果提供稳定输入。

2. 每个 case 采用最小 smoke 开关集合，并显式列出预期串口 marker。

   理由：BigOS 当前处于单核 research kernel 阶段，组合过宽会让 memory、IRQ、filesystem、user-mode path 的失败相互掩盖。备选方案是提供一个 all-smokes 配置，但它会扩大 proc smoke 编译边界，也会使故障定位变差。

3. 自动化 runner 优先使用 QEMU headless marker 检查。

   理由：QEMU headless 能在无交互 display 的环境中捕获 COM1 日志并按 marker bounded 退出，适合作为可重复 smoke。备选方案是默认 Bochs，但 Bochs 的 ROM/display 配置更依赖本机环境；该 change 仍要求在 boot、IRQ、timer、ATA PIO、port IO 相关变更中记录 Bochs 或双 emulator 复核结果（若可用）。

4. 结构化 validation artifact 第一版输出 Markdown，并保留 JSON schema 兼容字段。

   理由：Markdown 便于 review，适合作为runtime smoke validation matrix 的第一版人工审查产物；同时在字段设计上保留 case id、status、tool availability、marker、log path、skip reason、residual risk 等 JSON schema 兼容结构，便于后续 CI 消费。备选方案是第一版同时输出 Markdown 和 JSON，但会增加格式同步成本；另一个备选方案是只打印终端日志，但终端日志不利于后续归档和审查。

5. 矩阵 runner 作为 `tools/boot_debug.py` 的子命令扩展，而不是新增独立 helper。

   理由：`tools/boot_debug.py` 已经负责 Legacy BIOS/MBR/exFAT image、QEMU/Bochs backend、serial marker 和 generated output，扩展子命令可以复用现有 preflight、image generation、emulator launch 和 marker wait 逻辑。备选方案是新增 `tools/runtime_smoke_matrix.py`，但会复制 boot debug helper 的 emulator/image 编排知识。

6. 每个 matrix case 支持独立 timeout，filesystem 和 user ELF 等慢路径使用更长默认值。

   理由：memory/timer/scheduler/syscall 等 case 应保持短超时以快速反馈，filesystem 和 filesystem-backed user ELF case 涉及 image packaging、ATA PIO/exFAT read 和 user ELF path，默认 timeout 应独立配置以避免误判。备选方案是统一 timeout，但它要么拖慢快速 case，要么让慢路径更容易 flaky。

7. Runner 不直接修改内核 runtime 语义，只编排构建配置、helper 参数和结果收集。

   理由：runtime smoke validation matrix 是验证产品化，必须保持 boot address、disk layout、interrupt vector、syscall vector、CR3 切换、user process smoke gating 和 marker 字符串稳定。任何 runtime 行为修复都应作为独立 change 提出。

## Risks / Trade-offs

- 矩阵过宽或 timeout 过长导致执行时间变长 -> 第一版只覆盖最小有价值组合，避免全组合笛卡尔积，并为慢路径配置独立 timeout。
- `xmake f` 配置在 case 之间残留 -> runner 必须在每个 case 前显式设置需要的 smoke，并在文档中说明如何恢复默认配置或记录当前配置。
- QEMU 与 Bochs 行为差异掩盖硬件边界问题 -> 对 boot、IRQ、timer、ATA PIO、port IO 相关变更保留 Bochs 或 QEMU+Bochs 交叉验证要求，并在不可用时记录剩余风险。
- 本地缺少 `x86_64-elf-*`、QEMU、Bochs、ROM/display 或 `uv` -> validation artifact 必须记录缺失工具、跳过 case、已执行替代检查和剩余风险，不得把未运行的 smoke 标记为通过。
- User-mode 和 filesystem-backed ELF smoke 编译 `kernel/core/proc/**` -> 矩阵必须将这些 case 与普通 boot/runtime case 分开，避免误认为 proc subsystem 是默认内核路径。
- 串口 marker 缺失可能来自启动失败、超时或配置错误 -> runner 输出必须包含 expected marker、serial log path、timeout/exit 状态和失败阶段，便于定位。

## Migration Plan

1. 新增 runtime smoke matrix 定义和 Markdown-first 结构化结果格式，先覆盖现有 smoke 开关和 marker，不新增 kernel marker。
2. 扩展 `tools/boot_debug.py` 子命令，使其按矩阵逐项执行 QEMU headless marker check，并记录 skipped/failed/passed。
3. 将生成的日志和 validation artifact 放入 `build/test/` 或明确指定的 output path。
4. 更新文档，说明runtime smoke validation matrix 的推荐执行入口、矩阵内容、跳过记录方式，以及 Bochs/QEMU cross-validation 使用场景。
5. 保留现有 `xmake run qemu`、`xmake run bochs` 和 `tools/boot_debug.py` 单项调试入口，runner 失败时开发者可以回退到单 case 手工执行。
