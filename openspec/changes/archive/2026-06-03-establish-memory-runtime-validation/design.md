## Context

BigOS 的内存管理已经完成基础 correctness 修复、页数/order API 拆分和 VMem map/unmap 生命周期整理。当前剩余风险在于验证信号仍偏静态：源码级测试和构建能发现部分回归，但不能证明 emulator 中 `init_mem()` 之后 `kmalloc/free`、kernel virtual pages 和 physical order 分配在真实页表环境下可用。

本设计聚焦 `src/mm` 与 `src/kernel/kernel.cc` 之间的早期启动验证链路。目标是在不引入 scheduler、IRQ enable、SMP 或新 allocator 策略的前提下，为内存管理建立可重复 runtime smoke。

## Goals / Non-Goals

**Goals:**

- 在 `init_mem()` 后提供可开关的 `bigos::mm::self_test()` 或等价入口。
- 覆盖 slab small-object、kernel virtual pages、physical order 分配释放和统计恢复。
- 提供 emulator 可观察 marker，使工具可以判断 self-test 是否执行并通过。
- 保留现有源码级测试、构建和 boot asset 生成验证，并把 runtime smoke 纳入任务闭环。

**Non-Goals:**

- 不改变 `kmalloc()`、`alloc_kernel_pages()`、`free_pages()` 或 `alloc_physical_order()` 的语义。
- 不新增 scheduler、IRQ enable、SMP、锁或 per-CPU allocator。
- 不实现 direct map、页表页回收、用户态地址空间或新的 slab 生命周期策略。
- 不把 debug-only invariant scanner 当作测试覆盖的替代品。

## Decisions

### Decision: 使用编译期开关控制 runtime self-test

默认内核路径应保持精简，runtime self-test 通过 `BIGOS_MM_SELF_TEST` 或 xmake 选项启用。这样可以在开发验证和正式 boot 路径之间保持清晰边界。

替代方案是总是运行 self-test。该方案能更早暴露问题，但会污染后续功能 bring-up 的启动路径，也可能让验证代码成为 runtime 依赖。

### Decision: self-test 放在 `init_mem()` 之后、IRQ 初始化之前

测试需要 VMem、buddy、slab 都已初始化，但不需要 IRQ、PIC 或 keyboard。放在 IRQ 初始化前能保持单核关中断环境，减少并发和中断重入变量。

替代方案是在 `init_mem()` 内部自动执行。该方案耦合更强，失败时更难区分初始化失败和测试失败。

### Decision: 使用串口文件 marker 作为 bounded Bochs oracle

当前 `kprintf()` 只写 VGA 文本，generated bochsrc 已确认使用 `com1: enabled=1, mode=null` 与 `log: -`。因此现有工具链能生成 boot image 和 Bochs config，但还没有可自动匹配 kernel marker 的稳定 runtime oracle。

本 change 将收敛为串口文件 marker：启用 self-test 的 boot smoke 应让成功 marker（例如 `MM self-test passed`）写入 COM1，并让 generated bochsrc 在 bounded smoke 模式下把 COM1 输出落到文件，工具侧轮询该文件匹配 marker。VGA 文本保留为人工观察路径；generated bochsrc 的 Bochs 日志只用于诊断 emulator/ROM/配置问题，不作为 kernel reachability oracle。

替代方案是只依赖 VGA、Bochs 日志、退出码或 Bochs 交互状态。VGA 不适合无头自动匹配；Bochs 日志默认不包含内核 `kprintf()` 文本；当前内核没有 hosted OS 退出机制，退出码不可用。

### Decision: 统计验证以可恢复分配为主

self-test 应记录 `g_nr_free_pages()` 和 VMem free counter 的前后变化，确保测试释放后恢复。VMem free counter 采用最小内部只读 accessor 暴露给 `src/mm` 内的 self-test：该 accessor 只返回计数值，不提供修改能力，优先放在 `bigos::mm::__detail`、私有头或 test-only 编译路径中，不进入通用 public API。

替代方案是只检查分配返回非空。该方案不足以发现泄漏、未释放 backing 或统计漂移。

### Decision: self-test 保持集中实现，不直接耦合 VMem 私有静态状态

内存 self-test 应作为 `src/mm` 内的集中测试入口维护，避免为了读取 VMem free counter 把测试逻辑拆入 `vmem.cc` 或直接触碰其私有静态变量。集中实现便于串联 slab、kernel virtual pages 和 physical order 的前后统计检查，也能让启动接入点只依赖单一 self-test 入口。

替代方案是把 VMem 相关检查放在 `vmem.cc` 内直接访问计数器。该方案短期更少接口，但会让 self-test 分散到 allocator 实现文件中，增加后续调整测试顺序、失败 marker 和阶段诊断的维护成本。

## Risks / Trade-offs

- Runtime self-test 本身可能引入 boot 依赖 -> 使用编译期开关，并保持测试代码只依赖 `src/mm` 基础 API。
- Bochs oracle 可能受本机 ROM、串口配置或本地交互限制影响 -> 同时保留 `boot_debug.py --no-launch`、构建和源码级测试，并在验证记录中明确缺口。
- 统计 getter 可能扩大内部 API 面 -> 优先放在 `bigos::mm::__detail` 或 test-only 编译路径，避免长期 public API 污染。
- 测试分配模式覆盖有限 -> 先覆盖最小关键路径，后续 slab lifecycle change 再扩大压力测试。

## Migration Plan

1. 增加 self-test 声明、实现和编译开关，默认不改变现有启动行为。
2. 在 kernel early path 中按开关调用 self-test，并输出固定 marker。
3. 增加工具侧串口文件 bounded Bochs smoke 或记录当前环境无法自动判断的原因。
4. 扩展源码级测试，禁止 self-test 无条件运行或依赖 scheduler/IRQ。
