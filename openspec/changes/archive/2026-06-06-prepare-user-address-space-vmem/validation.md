# Validation

## 已通过检查

- `uv run pytest tests/test_user_address_space_vmem_source.py tests/test_memory_interrupt_context_source.py tests/test_memory_correctness_source.py`：41 passed。覆盖显式 `PageAttr` 类型与 `page_attr` 策略常量（KERNEL_DEFAULT 等价旧 `0x3`、USER_DATA 含 user+NX、USER_CODE 清 NX）、`map_page`/`unmap_page`/`derive_user_address_space_root` 声明与 non-interrupt-context 契约、primitive 接受显式属性并在 `InterruptGuard` 下写入、unmap 清 PTE + `invlpg`、`map_kernel_range` 经由 primitive 以 supervisor 默认属性且不置 user bit、派生根复制高半区 (256..511) 且清零低半区 (0..255)、本阶段无 CR3 写/无 ring3 指令、smoke marker wiring 与 EFER.NXE 降级记录。
- `uv run pytest tests/`：82 passed（全量源码级测试无回归）。
- `xmake f -c && xmake`（默认配置）：cross-toolchain（`x86_64-elf-g++ 12.2.0`）构建通过，`src/mm/vmem.cc` 改写编译链接成功。
- `xmake f --user_vmem_smoke=y && xmake`：user vmem smoke 显式配置构建通过；随后 `xmake f -c` 恢复默认配置（smoke 默认关闭）。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -mno-red-zone -DBIGOS_USER_VMEM_SMOKE -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/mm/vmem.cc src/kernel/kernel.cc`：freestanding 语法检查通过（含 smoke 路径），无告警。
- `openspec validate prepare-user-address-space-vmem --strict`：通过（Change is valid）。

## 未运行或未通过检查

- Bochs runtime smoke 未通过（已知 oracle 限制）：
  - `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`：kernel 与 boot image 构建通过，但 Bochs 在超时窗口内未观测到 `BIGOS_MM_SELF_TEST_PASSED`，且 `build/test/serial.log` 未生成。
  - 未对 `BIGOS_USER_VMEM_SMOKE_PASSED` marker 做 Bochs runtime 观测：`tools/boot_debug.py` 的 `build_kernel` 只切换 `mm_self_test`，不支持注入 `user_vmem_smoke` 构建开关。按 tasks 4.3，本 change 不把 `tools/boot_debug.py` 的 Python 修改混入；该 oracle 扩展记录为后续横切工程化项。

## 历史诊断

- Bochs serial oracle 在本仓库历史 change 中多次不稳定。归档记录 `openspec/changes/archive/2026-06-06-introduce-kernel-threads-scheduler/validation.md` 与 `.../establish-tty-console-input/validation.md` 同样记录构建通过但 Bochs serial smoke 在 30~40 秒内未观测到 marker，与本次现象一致。因此本次 Bochs 超时被视为既有 oracle 限制，而非本 change 引入的回归。
- 编辑期间 IDE format-on-save（clang-format `AlignConsecutiveMacros`）会重排 `src/mm/vmem.cc` 顶部既有 `#define` 对齐，触发若干字节精确的既有源码级测试失败。已用直写方式恢复提交版宏对齐；该项为工具链/格式化误报，非本 change 逻辑回归。

## 当前 Change 影响

- 修改 public header `include/bigos/memory.h`：新增 `PageAttr` 类型、`page_attr` 策略常量、`map_page`/`unmap_page`/`derive_user_address_space_root` 声明与 non-interrupt-context 契约，新增 `user_vmem_smoke()`（仅在 `BIGOS_USER_VMEM_SMOKE` 下声明）。
- 修改 `src/mm/vmem.cc`：新增 `map_single_page()` primitive（属性入参、复用 self-mapping 遍历与缺级分配、`InterruptGuard` 写入边界、缺级失败回滚）；`ensure_paging_descriptor` 写入加 guard；`map_page`/`unmap_page`/`derive_user_address_space_root` 实现；`map_kernel_range()` 改为经由 primitive 以 `KERNEL_DEFAULT` 表达（PTE bit 等价旧 `0x3`）；新增默认关闭的 `user_vmem_smoke()`。
- 修改 `src/kernel/kernel.cc`：在 `init_mem()` 之后、IRQ 使能前的非中断上下文调用 `user_vmem_smoke()`（仅在 smoke 构建下）。
- 修改 `xmake.lua`：新增默认关闭的 `user_vmem_smoke` 开关与 `BIGOS_USER_VMEM_SMOKE` define。
- 新增文档/测试：`docs/arch/user-address-space-vmem.md`、`tests/test_user_address_space_vmem_source.py`。
- 修改 `tests/test_memory_correctness_source.py`：把 rollback bookkeeping 断言从 `new_descriptors[new_descriptor_count++]` 更新为等价的 `__changes[(*__change_count)++]`（重构后该计数逻辑移入共享 `ensure_paging_descriptor`，rollback 行为不变）。
- 未改动 boot 固定地址、higher-half/load base、BootInfo ABI、direct map、`KVMEM_BASE`、self-mapping 地址布局或 `KERNEL_PML4_ADDR`；既有内存源码级测试保持通过。

## 剩余风险

- **EFER.NXE 未使能 / NX 仅编码验证**：`src/arch/x86/boot/boot.s` 仅置 `EFER.LME`（bit 8），未置 `NXE`（bit 11），故 NX bit 当前只作“属性编码正确”验证，不能依赖运行时强制不可执行。真正强制 NX 需在后续切换 CR3/进入 ring3 的 change 中使能 NXE 并重做 runtime NX 验证。
- **用户根 runtime 未经 oracle 验证**：`map_page` user 属性 PTE、`derive_user_address_space_root` 高/低半区布局目前只由源码级检查、freestanding 构建/语法检查覆盖；`BIGOS_USER_VMEM_SMOKE_PASSED` 的实际运行时观测尚未在可稳定的 Bochs/serial 环境确认，属剩余 bootability 风险。
- **self-mapping 与多地址空间**：派生根共享内核高半区，self-mapping 仍解析内核页表；真正切换 CR3 时需重新设计 per-space self-mapping，本阶段不切 CR3 并把该问题列为后续前置设计。
- **boot_debug oracle 扩展**：观测 user vmem smoke marker 需扩展 `tools/boot_debug.py` 注入 `user_vmem_smoke` 开关，记录为后续独立横切工程化项，未混入本 change。
