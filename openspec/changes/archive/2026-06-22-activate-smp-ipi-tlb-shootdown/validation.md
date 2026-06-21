## 验证记录

### 已执行

- `xmake f --tlb_shootdown_smoke=y`：通过，专用 TLB shootdown smoke build switch 可配置，默认关闭。
- `PATH=/usr/local/bin/cross_compiler/bin:$PATH xmake`：通过，完整构建成功。
- `clang++ --target=x86_64-elf -fsyntax-only ...`：通过，覆盖本 change 修改过的 C++ source/header 依赖路径，包括 `kernel/core/kernel.cc`、`kernel/core/bigos/smp_ipi.cc`、`kernel/mm/mm_context.cc`、`kernel/mm/vmem.cc`、`kernel/core/proc/proc.cc` 等关键文件。
- `clangd --check=kernel/core/bigos/smp_ipi.cc --compile-commands-dir=.`、`clangd --check=kernel/mm/mm_context.cc --compile-commands-dir=.`、`clangd --check=kernel/mm/vmem.cc --compile-commands-dir=.`、`clangd --check=kernel/core/proc/proc.cc --compile-commands-dir=.`：均可加载 compile database 并构建 AST；Apple clangd check mode 在 tweak 阶段报告 `ExtractFunction` / `SwapBinaryOperands` 失败，归类为 clangd 辅助功能 false positive，不是源码编译诊断。`clang++ -fsyntax-only` 与 `xmake` 已覆盖当前变更引入的 C++ 类型/语法错误。
- `PATH=/usr/local/bin/cross_compiler/bin:$PATH uv run python tools/boot_debug.py run --emulator qemu --display none --skip-build --serial-log build/test/tlb-shootdown-qemu-smp.log --expect-serial-marker BIGOS_TLB_SHOOTDOWN_SMOKE_PASSED --smoke-timeout 60 --qemu-extra "-smp 2 -cpu max"`：通过，观测到专用 TLB shootdown smoke marker。
- `PATH=/usr/local/bin/cross_compiler/bin:$PATH uv run python tools/boot_debug.py run --emulator qemu --display none --skip-build --serial-log build/test/tlb-shootdown-qemu-smp-user.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 60 --qemu-extra "-smp 2 -cpu max"`：通过，bounded userland baseline 到达 `BIGOS_USER_EXEC`。同一日志中观测到 `BIGOS_TLB_SHOOTDOWN_SMOKE_AP_RESIDENT`、`BIGOS_TLB_SHOOTDOWN_IPI_DELIVERED`、`BIGOS_TLB_SHOOTDOWN_IPI`、`BIGOS_TLB_SHOOTDOWN_COMPLETE`、`BIGOS_TLB_SHOOTDOWN_SMOKE_PASSED`、`BIGOS_MM_CONTEXT_RESIDENT`。
- `PATH=/usr/local/bin/cross_compiler/bin:$PATH uv run python tools/boot_debug.py run --emulator bochs --display none --skip-build --serial-log build/test/tlb-shootdown-bochs-smp.log --expect-serial-marker BIGOS_TLB_SHOOTDOWN_SMOKE_PASSED --smoke-timeout 60 --bochs-cpus 2`：通过，Bochs 多核交叉 smoke 观测到专用 TLB shootdown smoke marker。
- `PATH=/usr/local/bin/cross_compiler/bin:$PATH uv run python tools/boot_debug.py run --emulator bochs --display none --skip-build --serial-log build/test/tlb-shootdown-bochs-smp-user.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 60 --bochs-cpus 2`：通过，Bochs 多核下 bounded userland baseline 到达 `BIGOS_USER_EXEC`。串口日志存在并发输出交织，但仍观测到 TLB shootdown completion、smoke passed、`BIGOS_MM_CONTEXT_RESIDENT` 和 `BIGOS_USER_EXEC`。
- `openspec validate activate-smp-ipi-tlb-shootdown --strict`：通过，proposal、design、spec delta 和 tasks 均可被 OpenSpec strict validation 解析。
- `rg -n "Stage [0-9]|stage [0-9]|阶段[0-9]|阶段 [0-9]" openspec/changes/activate-smp-ipi-tlb-shootdown`：无输出，未发现阶段编号依赖。
- `rg -n "(Stage [0-9]|stage [0-9]|阶段[[:space:]]*[0-9]+|roadmap|路线图|Task M[0-9]+|M[0-9]+\.[0-9]+)" openspec/changes/activate-smp-ipi-tlb-shootdown/proposal.md openspec/changes/activate-smp-ipi-tlb-shootdown/design.md openspec/changes/activate-smp-ipi-tlb-shootdown/specs openspec/changes/activate-smp-ipi-tlb-shootdown/tasks.md`：无输出，未发现路线图阶段编号、roadmap 或 Task M 编号依赖。

### 未执行或未通过

- 无。

### 源码审查结论

- IPI 分类：scheduler nudge 与 TLB shootdown 通过 `bigos::smp::IpiType` 分类，TLB shootdown 使用独立 `VECTOR_TLB_SHOOTDOWN`，LAPIC EOI 仍由 `irq_dispatch()` 统一执行。
- IRQ-safe 边界：TLB shootdown IPI handler 只读取固定 request slot、执行本地 invalidation、更新 ack bit；不分配内存、不进入 VFS/block I/O、不等待 scheduler blocking primitive。
- `mm context` residency：`MmContext` 记录 address-space root、引用计数、active CPU mask、teardown flag 与 shootdown generation；用户地址空间切换通过 `enter_mm_context()` / `leave_current_mm_context()` 维护 residency。
- Shootdown ordering：PTE 更新之后调用 `invalidate_tlb()`；requester 发布固定 slot 后发送 typed IPI，并在 bounded timeout 内等待 ack。缺失 delivery 或 ack 会进入 deterministic panic path，避免继续释放依赖 shootdown 的 frame/page-table page。
- VM 接入：匿名 unmap/protect、fork COW 降权、COW fault 写权限恢复、exec replacement 与 reap teardown 已接入 `mm context` 或 residency 判断；inactive teardown 依赖 active root 与 active CPU mask 排除远端 residency。

### 残余风险

- frame refcount 与 shared-readonly metadata 仍沿用既有 bounded 结构；本 change 对释放顺序接入了 shootdown completion，但没有把整个 frame/share cache 子系统升级为通用 SMP 数据结构。
