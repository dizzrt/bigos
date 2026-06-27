## 1. 边界盘点与 ABI 设计

- [x] 1.1 盘点既有内核 ELF 装载路径（`kernel/core/proc/proc.cc` 的 `validate_elf`/`map_segment`/段描述结构）、对 `PT_DYNAMIC`/`PT_INTERP`/`PT_TLS` 与 `ET_DYN` 的既有拒绝点，以及 `execve`/`launch_init` 提交路径，确认动态加载分支接入点。
- [x] 1.2 盘点 `copy_exec_args_to_stack` 初始栈构造（`[argc][argv][NULL][envp][NULL][strings]`）与既有静态 crt0（`user/crt0/crt0.s`）读取假设，确认 auxv 追加式扩展的接入点与对静态路径的透明性。
- [x] 1.3 盘点 `include/bigos/proc.h` 用户 VM 布局常量（`USER_CODE_BASE`/`USER_STACK_TOP`/`USER_ANON_BASE`/`USER_FILEMAP_BASE`/`USER_RUNTIME_RESERVED_BASE/END`）与 `user-runtime-vm-layout` 既有预留 gap，确定动态主镜像/解释器/共享对象映射区的确定性有界基址与上限。
- [x] 1.4 盘点 `include/bigos/elf.h` 既有 `ET_DYN`/`PT_INTERP`/`PT_DYNAMIC`/`DT_*` 与重定位结构（`Elf64_Rela`/`Elf64_Dyn`/`Elf64_Sym`），确认需补齐的 `AT_*` auxv 常量、`Elf64_auxv_t`、`R_X86_64_*` 重定位类型与 `ELF64_R_TYPE/SYM` 宏。
- [x] 1.5 审查启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换与既有静态 `ET_EXEC` 装载 ABI 与 syscall number，确认本变更不修改这些边界（仅追加式扩展初始栈与新增有界布局常量）。

## 2. 内核头与布局常量

- [x] 2.1 在 `include/bigos/elf.h` 补齐 auxv（`AT_NULL`/`AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ` 等）、`Elf64_auxv_t`、所需 `R_X86_64_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`/`R_X86_64_64` 与 `ELF64_R_TYPE`/`ELF64_R_SYM` 宏，保持公开头最小化。
- [x] 2.2 在 `include/bigos/proc.h` 新增有界动态布局常量（动态主镜像基址、解释器基址、共享对象映射区基址与最大页数）与有界上限（`DT_NEEDED` 数、共享对象数、重定位条目数、auxv 项数），并确认落在受支持用户低半区且与既有区不冲突。

## 3. 内核有界动态加载路径

- [x] 3.1 扩展 `validate_elf`：在默认关闭守卫（如 `BIGOS_DYNAMIC_LINK`）内识别含恰好一个 `PT_INTERP` 的有界 `ET_DYN` 主镜像，产出主镜像段描述与解释器路径，保留全部既有越界/重叠/对齐/W^X/入口可执行校验（按基址偏移调整地址校验）；静态 `ET_EXEC` 路径零改动。
- [x] 3.2 经既有内核 VFS 读路径读取 `PT_INTERP` 指定解释器 ELF（有界 `ET_DYN`），校验其头与 program header，产出解释器段描述；解释器缺失/超界/读取失败/非法走确定性失败。
- [x] 3.3 扩展段映射，支持按确定性基址偏移（`load_base + p_vaddr`）映射 `ET_DYN` 段，把主镜像与解释器分别映射到各自基址，纳入 VMA 集合与 `user-runtime-vm-layout` 布局描述；越界/重叠确定性拒绝并经既有 safe-release 回收。
- [x] 3.4 扩展 `copy_exec_args_to_stack`：动态路径在 `envp` NULL 后追加有界 auxv（填充 `AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL`），其中 `AT_PHDR` 用"扫描 `PT_LOAD` 找覆盖 `e_phoff` 段"的算法计算并在 `validate_elf` 强制 phdr 区间须完整落在某只读 `PT_LOAD` 内（见决策 3，不强依赖 `PT_PHDR`）；静态路径保持透明；超界确定性失败回滚。
- [x] 3.5 在 `execve`/`launch_init` 动态路径选择解释器入口（`USER_INTERP_BASE + e_entry`）为 ring3 entry point，保持原子提交边界与失败回滚；审查绝不在 IRQ 上下文执行装载/读取。

## 4. 用户态动态链接器 ld.so

- [x] 4.1 新增 ld.so（`user/` 下），以 `-fPIC -Bsymbolic -fvisibility=hidden` 构建 `ET_DYN`（见决策 4）；实现 `_dl_start` 裸入口：用 GOT[0] + PC-relative 技巧求自身 load bias（`AT_BASE` 仅作交叉校验），据此遍历自身 `PT_DYNAMIC` 仅处理自身 `R_X86_64_RELATIVE` 完成自重定位，自重定位完成前不访问依赖 GOT 的全局量/函数（以 `-fno-plt`/小型可审查实现约束）。
- [x] 4.2 解析主镜像 `AT_PHDR`/`AT_PHNUM` 定位 `PT_DYNAMIC`，读取 `DT_STRTAB`/`DT_SYMTAB`/`DT_RELA`/`DT_RELASZ`/`DT_JMPREL`/`DT_PLTRELSZ`/`DT_NEEDED` 等有界动态信息。
- [x] 4.3 对有界数量 `DT_NEEDED` 共享库按约定绝对搜索路径（不实现 `LD_LIBRARY_PATH`）经 libc open/read/映射路径加载到共享对象映射区确定性基址，记录符号表；超界确定性失败。
- [x] 4.4 实现 eager（`BIND_NOW` 等价）重定位子集（`R_X86_64_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`/`64`）与有界全局作用域符号绑定（主镜像优先 + 加载顺序），弱符号缺失确定性绑定为 0，非弱符号缺失/未支持重定位类型/超界确定性失败并发 marker。
- [x] 4.5 完成重定位后跳转 `AT_ENTRY` 主镜像入口，按 System V x86_64 约定传递含 auxv 的原始初始栈；保持 freestanding-safe（无 hosted runtime/异常/RTTI/C++ 全局构造），仅经有界 libc 或 `int 0x80` 与内核交互。

## 5. 用户构建管线与示例产物

- [x] 5.1 在 `xmake/user_package.lua`（及所需链接脚本/lua）新增默认关闭开关（如 `dynamic_link_smoke`）下的 ld.so 构建（`-fPIC -ffreestanding -nostdlib` + 专用链接脚本，产出 `ET_DYN`），打包到约定解释器路径。
- [x] 5.2 新增示例共享库 `libdemo.so`（`-fPIC -shared`，导出少量函数/数据符号，刻意只用受支持重定位类型、无 TLS/IFUNC）。
- [x] 5.3 新增动态可执行示例 `dyn_demo`（`-fPIC -pie`，`DT_NEEDED libdemo.so`、`PT_INTERP` 指向 ld.so），调用 `libdemo.so` 导出符号；构建期校验示例产物重定位类型在支持子集内。
- [x] 5.4 确认默认关闭时不构建/打包 ld.so、`libdemo.so`、`dyn_demo`，默认磁盘镜像、默认 `/bin/*` 静态集合与默认启动不变。

## 6. 默认关闭验证与工具接入

- [x] 6.1 新增默认关闭动态链接 smoke 入口与确定性 marker（`BIGOS_DYNLINK_PASSED`/`BIGOS_DYNLINK_FAILED`），覆盖正常闭环（加载 `dyn_demo` → ld.so 自重定位 → 加载 `libdemo.so` → 绑定并调用导出符号 → 校验返回值）。
- [x] 6.2 覆盖失败路径：解释器缺失、`DT_NEEDED` 库缺失、未解析非弱符号、未支持重定位类型、超界、越界/冲突基址，确认确定性失败 marker 或统一 panic。
- [x] 6.3 跑默认启动回归，确认动态链接 smoke 关闭时 storage/filesystem/`/rw`/shell 与 userland baseline 不依赖任何动态链接组件并正常进入 shell（静态 `/bin/*` 与 PID-1 init 不变）。
- [x] 6.4 若新增或修改 Python host-side 验证辅助，使用 `uv run ...`，并补充 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`；若未改 Python，记录不适用。
- [x] 6.5 可用时跑 Bochs 默认启动交叉验证（Legacy BIOS，动态 smoke 关闭）；若 Bochs ROM/display/磁盘镜像路径不可用，记录无法运行原因与剩余风险。

## 7. 静态检查、构建与文档收尾

- [x] 7.1 运行 xmake 目标构建（x86_64-elf-gcc/x86_64-elf-g++/x86_64-elf-ld/as），含动态 smoke 开/关两种配置；确认交叉工具链支持 `-fPIC`/`-shared`/`-pie` 与动态段生成，若不可用记录 blocker、替代检查与剩余风险。
- [x] 7.2 运行接近 GCC 交叉构建环境的 clang 辅助检查（freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI）覆盖改动的内核 C++ 源；修复当前变更新增有效诊断，历史诊断与 freestanding false positive 分开记录。
- [x] 7.3 运行对应 clangd 辅助诊断或记录 clangd flags/config 差距；修复当前变更新增有效诊断，历史诊断与 false positive 分开记录。
- [x] 7.4 审查 ELF/装载/初始栈/布局相关 bootability、ABI/布局兼容性与地址假设：确认动态路径关闭时既有静态 `ET_EXEC` 装载、初始栈、syscall number 与 boot/页表/磁盘布局完全不变。
- [x] 7.5 审查内存/缺页/映射路径：动态段映射、解释器/共享对象映射区分配的初始化顺序、对象生命周期、对齐与失败回收，确认越界/冲突走确定性 safe-release，不泄漏页/页表页/VMA。
- [x] 7.6 更新 docs/en 与 docs/zh 镜像文档，描述有界动态链接边界（不声称完整 POSIX 动态链接器、`dlopen`/`dlsym`、TLS、`IFUNC`、符号版本或 lazy PLT），保持目录结构同构与仓库相对路径。
- [x] 7.7 实现完成后更新 `roadmap.md` 中 M12.1 完成状态，保持 roadmap 仅项目规划级描述，不加入入口点、命令、marker、文件路径或源码细节。
- [x] 7.8 运行 OpenSpec 校验与状态检查（`openspec validate add-bounded-dynamic-linking --strict`、`openspec status`），确认 artifacts/规格/任务处于可归档状态，并在验证记录中区分已通过、无法运行（含原因与剩余风险）、历史诊断与当前变更新问题。

## 验证记录

### 已通过

- xmake 构建（交叉工具链 `x86_64-elf-gcc 12.2.0` / `ld` / `as`）：动态 smoke 开
  （`xmake f --dynamic_link_smoke=y`）与关两种配置 `xmake`/`xmake build kernel`
  均成功；交叉工具链确认支持 `-fPIC`/`-shared`/`-pie` 与动态段生成（产出含
  `PT_DYNAMIC`/`PT_INTERP` 的 `ET_DYN`/PIE）。
- ld.so / libdemo.so / dyn_demo 产物校验：`ld-bigos.so` 为 `ET_DYN`、入口
  `_dl_start`、自身零重定位；`libdemo.so` 导出 `demo_add`/`demo_msg`/`demo_value`
  并带 SysV HASH；`dyn_demo` 为 PIE、`PT_INTERP=/lib/ld-bigos.so`、`DT_NEEDED
  libdemo.so`、仅含受支持的 `R_X86_64_GLOB_DAT` 重定位，无 TLS/IFUNC；构建期
  重定位子集守卫生效。
- 动态链接闭环 smoke（QEMU UEFI headless）：观察到
  `BIGOS_DYNLINK_PASSED`，serial 顺序为 `BIGOS_PROC_INIT` → `BIGOS_INIT_ENTER`
  → `BIGOS_USER_ENTER` → `BIGOS_DYNLINK_PASSED`（内核加载 `dyn_demo` + ld.so →
  ld.so 自重定位 → 加载 `libdemo.so` → 绑定并调用 `demo_add`/`demo_msg`/
  `demo_value` → 校验返回值）。
- 默认启动回归（动态 smoke 关闭）：QEMU UEFI headless 观察到 `BIGOS_USER_EXEC`；
  Bochs Legacy BIOS（`--boot-mode legacy --display none`）同样观察到
  `BIGOS_USER_EXEC`，确认默认启动不依赖任何动态链接组件、静态 `/bin/*` 与 PID-1
  init 不变、legacy exFAT `/lib` 改动不影响默认启动。
- clang 辅助检查（freestanding C++17、`-target x86_64-elf`、no exceptions/RTTI、
  项目 include，开 `BIGOS_DYNAMIC_LINK`/`BIGOS_DYNAMIC_LINK_SMOKE`）：
  `kernel/core/proc/proc.cc`、`kernel/core/syscall/syscall.cc`、`include/bigos/elf.h`
  均零诊断。
- Python host-side 辅助（`tools/bigosdev/core.py` /lib 打包、两处 source-count
  测试更新）：`uv run ruff check` 通过，`uv run ruff format --check` 通过，
  `uv run pyright tools/bigosdev/core.py` 0 errors，syscall-mirror 契约测试
  `tests/test_syscall_entry_source.py` 18 项全过（含新增 `SYS_DYN_MAP=59`/
  `SYS_DYN_PROTECT=60` 的内核/用户镜像一致性）。
- OpenSpec：`openspec validate add-bounded-dynamic-linking --strict` 与
  `openspec status` 通过（见收尾）。

### 无法运行 / 剩余风险

- 未做 ASLR/随机基址：动态主镜像/解释器/共享对象使用确定性固定基址（设计决策 2），
  这是有意的有界选择，剩余风险为缺乏地址随机化的安全收益（非本变更目标）。
- 真实磁盘镜像打包：UEFI（mtools）与 legacy exFAT 两条打包路径都新增了 `/lib`；
  动态闭环 smoke 仅在 QEMU UEFI headless 上端到端验证，legacy exFAT 的 `/lib`
  目录仅经默认启动回归（Bochs/QEMU）间接验证未破坏既有布局，未在 legacy 路径上
  端到端跑动态闭环；剩余风险为 legacy 动态闭环未直接覆盖。

### 历史诊断（与本变更无关，pre-existing）

- `uv run pytest` 全量存在 19 个 pre-existing 失败（在干净基线 `git stash` 后复跑
  确认与本变更无关，主要为既有 source-string 断言与既有头/源漂移）。本变更新增的
  2 个失败（`test_time_and_identity_source.py::test_identity_initialized_on_creation_and_inherited_on_fork`
  与 `test_signals_source.py::test_lifecycle_initializes_inherits_and_resets_signal_state`）
  源于新增的默认关闭 `create_dyn_user_process` 第三条非 fork 创建路径，已更新这两个
  source-count 断言（2 → 3）并通过；与之无关的 19 个历史失败不在本变更范围内修复。
