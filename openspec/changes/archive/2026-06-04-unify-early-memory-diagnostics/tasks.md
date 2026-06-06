## 1. 诊断设施核心

- [x] 1.1 新增 `include/bigos/panic.h`：声明 `bigos::PanicCode` 错误码枚举（按
  mm-buddy/mm-slab/mm-arena/mm-vmem/self-test/irq-exception/irq-pagefault/generic
  分域，boot 早期段按 D7 预留不连线）与 `[[noreturn]] noexcept` 的
  `kpanic(code, source, fmt, ...)` 入口，include 最小化。
- [x] 1.2 新增 `src/kernel/bigos/panic.cc`：实现 `kpanic`，先用常量字符串
  `serial_puts` 输出固定首行 `BIGOS_PANIC code=<code> source=<source>`（键=值空格
  分隔，code 十六进制，对齐 `BIGOS_EXCEPTION` 风格），再 `kprintf` 输出变参上下文到
  后续行（串口与 VGA），最后 `disableIRQ()` + `while(true){hlt}`。
- [x] 1.3 提供“仅 marker + 停机”的轻量重载/宏，以及可选 `kpanic_with_mm_stats`
  形式（复用现有只读统计源，禁止动态分配）。
- [x] 1.4 确认 `panic.cc` 被 `xmake.lua` 的 `src/kernel/**.cc` glob 纳入构建。

## 2. 中断与异常路径收敛

- [x] 2.1 `src/kernel/irq/interrupt.cc`：将 `halt_cpu()` 改为复用统一关中断+停机原语，
  保留 `BIGOS_EXCEPTION` 与 `BIGOS_PAGE_FAULT` 诊断输出不变。
- [x] 2.2 复查异常/`#PF`/unknown vector 路径：致命停机统一经由 `kpanic`/停机原语，
  非致命路径（`default_external_irq_handler`、`unknown_vector_handler`）行为不变。

## 3. 内存致命路径收敛

- [x] 3.1 `src/mm/buddy.cc`：`halt_memory_handoff_failed()` 与
  `halt_early_metadata_exhausted()` 改用 `kpanic`，补 `BIGOS_PANIC` 前缀与错误码，
  保留 arena 用量诊断为上下文行；停机前先关中断。
- [x] 3.2 `src/mm/slab.cc` 与 `src/mm/kmem.cc`：`BIGOS_SLAB_DEBUG` guard 的 halt 分支
  改用 `kpanic`，将 `"slab debug guard: ..."` 转为带前缀的 panic 上下文与错误码。
- [x] 3.3 `src/mm/self_test.cc`：`fail(stage)` 改用统一停机原语，保留
  `BIGOS_MM_SELF_TEST_FAILED stage=<stage>` 输出契约不变。
- [x] 3.4 全仓核对 `src/kernel`、`src/mm`、`src/kernel/irq` 不再残留缺关中断或缺
  `BIGOS_` 前缀的独立 `while(true){hlt}` 致命片段；`kernel()` 末尾 idle 循环属正常
  停机非致命路径，保持不变；boot 早期路径（`src/arch/x86/boot/*`）按 D7 不纳入本
  change，保留现状。

## 4. 内存安全/中断安全审查

- [x] 4.1 审查 `kpanic` 在内存损坏或 arena 耗尽场景下的路径：首行 marker 不依赖变参
  格式化；快照路径不分配内存、不触发可能再次失败的调用。
- [x] 4.2 审查关中断时机：所有迁移后的致命路径均在 `hlt` 前 `cli`，与单核早期关中断
  假设一致；确认不改变成功路径的中断状态。

## 5. 验证

- [x] 5.1 运行最窄交叉构建：`xmake`（默认配置）成功；记录结果。
  （`build ok`，仅遗留预先存在的宏空白告警与 RWX LOAD segment 告警。）
- [x] 5.2 运行带开关构建：`xmake f --mm_self_test=y` 与 `xmake f --slab_debug=y`
  及 `--page_fault_smoke=y` 各自构建成功；记录结果。（三者均 `build ok`。）
- [x] 5.3 C++ 辅助静态检查：对修改的 C++ 文件运行 clang/clangd（按 freestanding C++17、
  x86_64、项目 include、无异常/RTTI 配置），修复本 change 引入的错误并确认或修复新增
  有效告警；区分历史诊断、本次诊断与 freestanding/toolchain 误报。
  （IDE/clangd 诊断：本 change 修改文件无新增错误或告警。）
- [x] 5.4 Bochs panic 停机 smoke：`uv run python tools/boot_debug.py run
  --page_fault_smoke ...`（或等价路径）观察致命停机 marker 与安全停机；不可用时记录
  缺失依赖与剩余 bootability 风险。
  （缺口：本机 Bochs 3.0 使用 term GUI，在 harness 无交互 TTY 下 `quit_sim exit code 1`
  立即退出，未生成 COM1 serial.log；与既有 memory self-test smoke 文档记录的限制一致，
  非本 change 引入。剩余 bootability 风险：panic/停机 marker 的 runtime 观测待可稳定
  观测 VGA/serial 的 Bochs 环境复核。）
- [x] 5.5 核对 marker 兼容性：检查 `tools/boot_debug.py`（`--expect-serial-marker`）
  与 `tests/` 中的 marker 断言，确认 `BIGOS_MM_SELF_TEST_PASSED/FAILED`、
  `BIGOS_PAGE_FAULT`、`BIGOS_EXCEPTION` 未被破坏，新增 `BIGOS_PANIC` 可识别。
  （`uv run pytest` 全量 31 通过，含 interrupt/memory 源码断言；既有 marker 字符串
  保留，新 `BIGOS_PANIC` 首行采用键=值风格可被 `--expect-serial-marker` 识别。）
- [x] 5.6 若新增/修改 Python 文件：运行 `uv run ruff check`、`uv run ruff format
  --check`、`uv run pyright`、`uv run pytest`；`uv` 不可用时显式记录。
  （本 change 未新增/修改 Python 文件，仅运行 `uv run pytest` 验证源码断言未被破坏。）
- [x] 5.7 运行 `openspec validate unify-early-memory-diagnostics` 通过；整理验证记录，
  分列已通过检查、无法运行的检查与原因、历史诊断、本次引入问题。
  （`Change 'unify-early-memory-diagnostics' is valid`。已通过：默认/开关交叉构建、
  全量 pytest、clangd 无新增诊断、openspec validate。无法运行：Bochs runtime smoke
  （term GUI/headless 限制，见 5.4）。历史诊断：宏空白与 RWX segment 告警。本次引入
  问题：无。）

## 6. 文档

- [x] 6.1 视情况新增/更新 `docs/en/arch` 中的早期诊断/panic 说明（marker 与错误码约定），
  并在 README/AGENTS 涉及 marker 列表处补充 `BIGOS_PANIC`（如适用）。
  （新增 `docs/en/arch/early-kernel-diagnostics.md`；README 特性列表补充 `BIGOS_PANIC`
  说明。）
