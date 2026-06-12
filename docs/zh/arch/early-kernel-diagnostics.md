# 早期内核诊断与 panic

BigOS 提供统一的 freestanding-safe 早期致命诊断设施，把分散在 mm（buddy/slab/kmem/
self-test）与 irq（异常/`#PF`）中的致命停机路径收敛到单一入口，保证“输出诊断 → 关中断
→ 安全停机”的次序一致，并统一致命 marker 前缀。

## 入口

- 头文件：`include/bigos/panic.h`；实现：`kernel/core/bigos/panic.cc`；命名空间 `bigos`。
- `bigos::khalt()`：统一停机原语，先 `cli` 关闭可屏蔽中断，再进入 `hlt` 循环，
  `[[noreturn]]`。供已自行输出诊断 marker 的路径复用。
- `bigos::kpanic(code, source[, fmt, ...])`：先用常量字符串输出固定首行 marker
  `BIGOS_PANIC code=<code> source=<source>`（COM1 + VGA），再按需输出变参上下文，最后
  经 `khalt()` 停机。
- `bigos::kpanic_with_mm_stats(code, source)`：可选诊断快照变体，停机前复用只读
  `print_slab_stats()`，不分配内存、不触发可能再次失败的路径。

入口不依赖 heap 分配、异常、RTTI、scheduler、IRQ 上下文服务或 hosted runtime API。

## Marker 与错误码

- 固定首行：`BIGOS_PANIC code=<code> source=<source>`，采用与既有 `BIGOS_EXCEPTION`
  一致的 `键=值` 空格分隔风格，`code` 以十六进制打印，`source` 为稳定来源标识。
- 错误码取自 `bigos::PanicCode` 稳定枚举，按来源分域：mm-buddy、mm-arena、mm-slab、
  mm-vmem（预留）、self-test、irq-exception、irq-pagefault、generic。boot 早期段按设计
  预留，不在本设施接入。

## 既有 marker 兼容

迁移保留各路径既有诊断输出契约，仅为此前无前缀的致命路径补 `BIGOS_PANIC`：

- `BIGOS_EXCEPTION`、`BIGOS_PAGE_FAULT`：异常/`#PF` 诊断行不变，停机经统一原语。
- `BIGOS_MM_SELF_TEST_FAILED stage=<stage>`：self-test 失败输出不变，停机经 `khalt()`。
- buddy handoff 失败、early metadata arena 耗尽、`BIGOS_SLAB_DEBUG` guard：改为经
  `kpanic` 输出带 `BIGOS_PANIC` 前缀与稳定错误码的诊断。

## 接入范围

仅覆盖 kernel 运行时与 mm/irq 路径。boot 早期代码（`kernel/arch/x86/boot/*`）不在本设施
接入，保留其现有失败/停机方式。`kernel()` 末尾的 idle `hlt` 循环为正常停机非致命路径，
不受影响。
