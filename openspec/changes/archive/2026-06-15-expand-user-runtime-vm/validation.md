## 实现记录

- 当前实现边界盘点：用户 VM 主要集中在 `kernel/core/proc/proc.cc`，页表 root/map/unmap/teardown 和 COW frame refcount 位于 `kernel/mm/vmem.cc`，CPL3/CPL0 page-fault 分流位于 `kernel/core/irq/interrupt.cc`；本 change 未移动 boot 固定地址、higher-half、direct map、KVMEM、recursive self-mapping、syscall vector 或 IRQ EOI 路径。
- Runtime layout：`include/bigos/proc.h` 新增 `UserRuntimeLayout` 和 future-runtime reserved gap 常量；`Process` 现在携带已提交 layout，覆盖 ELF load、heap、anonymous、stack guard/growth、stack、argument area 和 future-runtime gap。
- Image commit：ELF/flat smoke image 在发布 VMA 前先初始化 runtime layout；`exec_current_from_elf_image` 继续使用 prepared process 作为 staging image，成功后一次性替换 root、layout、VMA、entry 和 initial stack，失败保留旧 image。
- VMA/page table：process-visible VMA 创建改走 layout-aware helper；user PTE 发布、safe-copy validation、`brk`、restricted anonymous mapping、CPL3 demand paging 和 COW split 均校验 VMA 与 runtime layout，不把 reserved gap、guard 或 out-of-layout 地址隐式物化。
- Dynamic-linking non-goals：现有 loader 继续拒绝 `PT_INTERP`、`PT_DYNAMIC`、`PT_TLS`、非 `ET_EXEC`、W+X 和 unsupported program headers；future-runtime gap 和 layout metadata 仅作为 inert 预留点，不创建额外映射或 dynamic-loader entry。

## 验证

- `uv run pytest tests/test_vma_user_memory_api_source.py tests/test_user_elf_program_loader_source.py tests/test_fork_copy_on_write_source.py`
  - 结果：通过，`24 passed`。
  - 覆盖：layout non-overlap/reserved-gap source checks、VMA/layout permission matching、image commit layout ownership、COW layout ownership、teardown ownership source checks。
- `xmake`
  - 结果：通过，kernel build ok。
  - 覆盖：当前 cross-toolchain C++ 编译和链接路径。
- VS Code diagnostics
  - 结果：`kernel/core/proc/proc.cc`、`include/bigos/proc.h` 无诊断。
  - 覆盖：辅助 clang/clangd 风格静态诊断。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/stage43-runtime-vm-serial.log --expect-serial-marker BIGOS_USER_EXEC`
  - 结果：通过，QEMU serial marker observed: `BIGOS_USER_EXEC`。
  - 覆盖：normal init/userland exec entry，包含 boot artifact、user init ELF 和 raw image 打包路径。
- `openspec validate expand-user-runtime-vm --strict`
  - 结果：通过，`Change 'expand-user-runtime-vm' is valid`。

## Handoff

- 未修改 `docs/en` 或 `docs/zh`，因此无需同步语言镜像。
- 已检查 `roadmap.md`：当前内容仍保持项目规划层级，已包含 bounded userland、anonymous demand paging、`fork`/COW、image replacement 和 dynamic-linking non-goals；本 change 不需要加入源代码入口、命令、marker 或版本索引。
- 剩余风险：本次仅运行 normal init/userland QEMU marker smoke；未额外运行 demand-paging、fork/COW、userland runtime 的全部默认关闭 smoke 组合，也未运行 Bochs cross-validation。
