## Context

BigOS 当前用户程序执行链路如下：用户构建管线（`xmake/user_package.lua`）用 `-ffreestanding -nostdlib -static -fno-pic -fno-pie` 把 crt0（`user/crt0/crt0.s`）+ 有界 libc（`user/libc/*`）静态链接为固定基址 `0x400000` 的 `ET_EXEC`（链接脚本 `user/link.lds`）。内核 ELF 装载器（`kernel/core/proc/proc.cc` 的 `validate_elf`/`map_segment`）只接受 `ELF_TYPE_EXEC`，并在 `validate_elf` 中显式拒绝 `PT_DYNAMIC`/`PT_INTERP`/`PT_TLS`。`copy_exec_args_to_stack` 构造初始用户栈，布局为 `[argc][argv...][NULL][envp...][NULL][strings]`，crt0 据此调用 `main(argc, argv, envp)`。用户 VM 布局常量在 `include/bigos/proc.h` 定义：`USER_CODE_BASE=0x400000`、`USER_STACK_TOP=0x800000`、`USER_ANON_BASE=0x1000000`、`USER_FILEMAP_BASE=0x4000000`，并有 `USER_RUNTIME_RESERVED_BASE/END` 预留 gap。`user-runtime-vm-layout` spec 已显式预留"未来动态链接扩展点"但当前运行时拒绝；`user-elf-program-loader` spec 显式不实现动态链接。

M12.1 要在这条静态链路之上，于默认关闭路径内引入"动态链接 + 共享库"机制。经评审决定：**重定位/符号绑定放在用户态 ld.so 解释器**（而非内核内重定位），内核只负责加载 `ET_DYN` 主镜像 + `PT_INTERP` 指定的解释器并经 auxv 移交控制权。本变更跨越内核装载/初始栈构造、用户运行时（新增 ld.so + 示例共享库/动态可执行程序）、用户构建管线三个边界，且涉及初始栈 ABI 追加扩展，因此在编码前固化技术决策。本变更不改变启动地址、内核链接地址、页表自映射地址、磁盘布局、IDT/syscall vector、CR3 切换或既有静态 `ET_EXEC` 装载 ABI。

## Goals / Non-Goals

**Goals:**

- 内核在默认关闭路径下接受有界 `ET_DYN` 主可执行镜像 + 单个 `PT_INTERP` 用户态解释器，按确定性基址分别加载二者 `PT_LOAD` 段，并跳转到解释器入口。
- 在既有初始栈之上追加有界 auxv 握手（`AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL`），使 ld.so 能定位主镜像 program header、自身加载基址与真实入口。
- 交付有界、freestanding-safe、自重定位的用户态 ld.so：解析 `PT_DYNAMIC`、加载有界数量 `DT_NEEDED` 共享库、执行有界 `R_X86_64_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`/`64` 重定位子集与全局符号绑定，随后移交主程序入口。
- 交付一个示例共享库 + 一个动态可执行程序作为端到端验证，证明跨模块符号解析可用；默认关闭，不改变默认 `/bin/*` 静态集合与默认启动。
- 把 `user-runtime-vm-layout` 既有预留 gap 在有界范围内转为运行时解释器/共享对象映射区，所有映射限制在受支持用户低半区且不与栈/堆/匿名/file-backed 区冲突。

**Non-Goals:**

- 不实现 `dlopen`/`dlsym`/`dlclose` 运行时 API、`LD_PRELOAD`/`LD_LIBRARY_PATH` 全量搜索、符号版本（versioned symbols）、GNU hash 全量、多解释器。
- 不实现 TLS（`PT_TLS`、`R_X86_64_DTPMOD64`/`DTPOFF64`/`TPOFF64`）、`IFUNC`/`R_X86_64_IRELATIVE`、lazy PLT 的完整 `_dl_runtime_resolve` 机制（首期 eager/`BIND_NOW`）。
- 不把默认 libc 拆为 `libc.so`，不改变默认 `/bin/*` 静态程序为动态链接，不引入 C++ 跨模块全局构造/异常语义。
- 不引入广泛 file-backed `mmap`、通用 `MAP_FIXED`、无界共享对象数量或无界重定位条目数。
- 不引入 hosted runtime；用户态只用本仓库有界 libc 与本变更新增的 freestanding ld.so。

## Decisions

### 决策 1：重定位放用户态 ld.so，内核只做"加载 + 移交"

内核新增有界动态加载路径：当镜像为 `ET_DYN` 且含恰好一个 `PT_INTERP` 时，内核（a）按确定性基址加载主镜像全部 `PT_LOAD` 段，（b）读取 `PT_INTERP` 路径、经既有 VFS 读取解释器 ELF（其自身为 `ET_DYN`），按另一确定性基址加载其 `PT_LOAD` 段，（c）构造含 auxv 的初始栈，（d）以**解释器入口**为 ring3 entry point。重定位与符号绑定完全由 ld.so 在用户态完成；内核不解析 `DT_*`、不写重定位、不解析符号。

- 备选：内核内完成全部重定位（首问选项 A）。否决（用户已选用户态 ld.so）：虽改动集中、无需 auxv/ld.so，但把动态链接策略固化进内核、扩展性差、偏离 POSIX 习惯。
- 备选：内核兼做部分重定位 + 用户态补全。否决：职责割裂，难以审查与界定边界。

内核侧改动集中在 `validate_elf`（按 `ET_DYN` + `PT_INTERP` 放宽校验并产出"主镜像 + 解释器"两组段描述）、段映射（带基址偏移映射）、`copy_exec_args_to_stack`（追加 auxv）、`execve`/`launch_init` 提交路径（选择解释器入口）。所有改动在 `BIGOS_DYNAMIC_LINK` 等默认关闭守卫内或对静态路径无副作用。

### 决策 2：确定性加载基址与布局，复用既有预留 gap

`ET_DYN` 主镜像与解释器均为位置无关，需要内核选定确定性、不冲突的加载基址。在 `include/bigos/proc.h` 新增有界常量（概念示意，最终值在实现时定）：

- `USER_DYN_EXEC_BASE`：动态主镜像加载基址（位于既有静态 `USER_CODE_BASE` 同区或独立有界区，确保 `PT_LOAD` 落在用户低半区、与栈/堆/匿名/file-backed 不冲突）。
- `USER_INTERP_BASE`：解释器（ld.so）加载基址，位于 `user-runtime-vm-layout` 既有预留的运行时 gap 内。
- `USER_DYN_LIB_BASE` + `USER_DYN_LIB_MAX_PAGES`：ld.so 用于映射 `DT_NEEDED` 共享对象的有界区域上界（ld.so 在该区内顺序选址）。

所有新区段加入 `user-runtime-vm-layout` 的布局描述与 VMA 集合，沿用既有 overflow/overlap/对齐/内核范围/栈冲突校验；失败按既有 safe-release 路径回收。`map_segment` 增加"基址偏移"参数，使 `ET_DYN` 段按 `base + p_vaddr` 映射，`p_vaddr` 仍校验为相对偏移有界。

- 备选：ASLR 随机基址。否决：当前无熵源需求且不利于确定性 smoke；固定确定性基址即可。

### 决策 3：初始栈追加有界 auxv（追加式 ABI 扩展）

在既有 `[argc][argv...][NULL][envp...][NULL]` 之后、字符串区之前追加 auxv 数组（`Elf64_auxv_t` 风格的 `{a_type, a_val}` 对），以 `AT_NULL` 终止。首期填充子集：

- `AT_PHDR`（主镜像 program header 在内存中的地址）、`AT_PHENT`（`sizeof(Elf64_Phdr)`）、`AT_PHNUM`（主镜像 phnum）
- `AT_ENTRY`（主镜像真实入口 = `USER_DYN_EXEC_BASE + e_entry`）
- `AT_BASE`（解释器加载基址 = `USER_INTERP_BASE`）
- `AT_PAGESZ`（`PAGE_SIZE`）、`AT_NULL`（终止）

`copy_exec_args_to_stack` 扩展为可选写入 auxv；静态 `ET_EXEC` 路径保持仅写 `argc/argv/envp/NULL`（不追加 auxv 或追加仅含 `AT_NULL` 的空向量，二者对既有静态 crt0 透明，因其只读到 `envp` 的 NULL 终止符为止）。该布局是内核与 ld.so 之间的契约，由 smoke 显式校验。

**`AT_PHDR` 计算采用现代 Linux 内核（`fs/binfmt_elf.c`）的"扫描 `PT_LOAD`"算法，而非朴素的 `base + e_phoff`。** 朴素写法在第一个 `PT_LOAD` 不从文件偏移 0 起、或 `e_phoff` 落在任何 `PT_LOAD` 之外时会指向未映射内存，ld.so 一访问即 `#PF`。正确算法是找到真正覆盖 `e_phoff` 的那个 `PT_LOAD`，用它的 `p_vaddr` 反推：

```
phdr_vaddr = 0; found = false
for each PT_LOAD p:
    if p.offset <= e_phoff && e_phoff + phnum*phentsize <= p.offset + p.filesz:
        phdr_vaddr = p.vaddr + (e_phoff - p.offset); found = true; break
if !found: reject(确定性 loader 错误)        # fail-closed
AT_PHDR = USER_DYN_EXEC_BASE + phdr_vaddr
```

即在 `validate_elf` 强制 "program header 表区间必须完整落在某个只读 `PT_LOAD` 段内，否则确定性拒绝"，这与现代内核的 fail-closed 行为一致；我们自构建的示例库/动态程序天然满足（首个只读 `PT_LOAD` 从 offset 0 起覆盖 ELF header + phdr）。

- 关于 `PT_PHDR`：遵循 System V gABI 与 Linux 内核的做法——**内核不强依赖 `PT_PHDR`**。若镜像存在 `PT_PHDR`，可用其 `p_vaddr` 交叉校验上面扫描得到的 `phdr_vaddr`；若不存在，扫描 `PT_LOAD` 的算法已足够，不要求工具链生成 `PT_PHDR`。

### 决策 4：ld.so 自重定位 + 有界重定位/符号绑定子集（eager 绑定）

ld.so 自身为 `ET_DYN`，入口为 `_dl_start`。**构建以 `-fPIC -Bsymbolic -fvisibility=hidden` 为核心**（`-shared -e _dl_start` 或 `-pie` 均可产出 `ET_DYN`，glibc 历史上用 `-shared`，是验证过的稳妥默认）：`-Bsymbolic` 让 ld.so 内部符号引用本地绑定、`-fvisibility=hidden` 让内部符号不进 `.dynsym`，二者共同把内部引用尽量收敛为 `R_X86_64_RELATIVE` 而非走全局符号表的 `GLOB_DAT`——这正是自举阶段能"只处理 RELATIVE"的前提（glibc/musl 同此思路）。控制流：

1. **ld.so 自举：用 GOT[0] 技巧求自身加载基址（不依赖 auxv）。** 自举时 ld.so 还无法靠 `AT_BASE` 之外的任何全局量定位自身——业界标准（glibc `elf_machine_load_address`、musl `_dlstart_c`）的做法是：链接器把 `GOT[0]` 填为 `_DYNAMIC` 的**链接时 vaddr**，`_dl_start` 用一段 PC-relative 汇编取得 `_DYNAMIC` 的**运行时地址**，两者相减即 load bias（自身加载基址）。`_dl_start`（汇编/裸入口）据此 bias 遍历自身 `PT_DYNAMIC`，仅处理自身 `R_X86_64_RELATIVE`（`*(base+r_offset) = base + r_addend`）完成自重定位，自重定位完成前绝不访问任何依赖 GOT 的全局量或函数。内核额外提供的 `AT_BASE` 作为**辅助/交叉校验**（应等于 GOT[0] 技巧算出的 bias），而非自举的唯一依据——这样即使内核 auxv 缺失/异常，ld.so 仍能自举，与标准实现一致。

   > 区分两类"找 phdr/`_DYNAMIC`"：ld.so 找**自身** `_DYNAMIC` 用 GOT[0] + PC-relative（不靠 auxv）；ld.so 找**主程序** phdr 用 auxv 的 `AT_PHDR`/`AT_PHNUM`。

2. 解析主镜像 `AT_PHDR`/`AT_PHNUM` 找到 `PT_DYNAMIC`，读取 `DT_STRTAB`/`DT_SYMTAB`/`DT_RELA`/`DT_RELASZ`/`DT_JMPREL`/`DT_PLTRELSZ`/`DT_NEEDED`/`DT_HASH`（或线性符号扫描）。
3. 对每个 `DT_NEEDED`（有界数量），按固定搜索路径（如约定的 `/lib` 绝对路径，不实现 `LD_LIBRARY_PATH`）经 libc `open/read/mmap`-等价路径加载共享对象到 `USER_DYN_LIB_BASE` 区内确定性基址，记录其符号表。
4. 执行重定位子集（eager / `BIND_NOW`，对每个对象处理 `DT_RELA` 与 `DT_JMPREL`）：
   - `R_X86_64_RELATIVE`：`*reloc = base + addend`
   - `R_X86_64_GLOB_DAT` / `R_X86_64_JMP_SLOT`：解析符号 → `*reloc = sym_value`
   - `R_X86_64_64`：`*reloc = sym_value + addend`
   符号解析按"主镜像优先 + 已加载库顺序"的有界全局作用域线性查找（首期不实现版本符号/STB_WEAK 完整语义，仅取首个 STB_GLOBAL 定义，weak 缺失绑定为 0）。
5. 跳转到 `AT_ENTRY`（主镜像真实入口），按 System V 约定把原始栈（含 auxv）传给主程序 crt0。

ld.so 不使用 lazy PLT 解析：通过 eager 绑定填好 GOT/PLT 槽，避免实现 `_dl_runtime_resolve` 与 GOT[1]/GOT[2] 蹦床。所有上限（`DT_NEEDED` 数、对象数、每对象重定位条目数、符号表大小）为确定性有界值，超界返回确定性失败并经 ld.so 诊断 marker 退出，绝不进入未定义跳转。

- 备选：仅靠内核 `AT_BASE` 求自身基址，不用 GOT[0] 技巧。否决：偏离 glibc/musl 标准自举路径，使 ld.so 自举强耦合于我们自定义 auxv；采用 GOT[0] 技巧 + `AT_BASE` 交叉校验更稳健、更贴近业界。
- 备选：lazy PLT 解析。否决（首期）：需要 `_dl_runtime_resolve` 汇编蹦床与运行时回调，复杂度高，eager 足以验证机制。
- 备选：GNU hash 查找。否决（首期）：线性/SysV hash 即可满足有界示例库符号规模。

### 决策 5：示例库 + 动态可执行程序 + 构建管线（默认关闭）

构建管线（`xmake/user_package.lua`）在默认关闭开关（如 `dynamic_link_smoke`）下额外构建：

- ld.so：`-fPIC -ffreestanding -nostdlib` + 专用链接脚本，产出 `ET_DYN` 解释器，打包到约定解释器路径（如 `/lib/ld-bigos.so`）。
- `libdemo.so`：`-fPIC -shared`，导出少量函数/数据符号（如 `demo_add`、`demo_msg`）。
- 动态可执行示例 `dyn_demo`：`-fPIC -pie`，`DT_NEEDED libdemo.so`，`PT_INTERP=/lib/ld-bigos.so`，调用 `libdemo.so` 导出符号；作为 smoke 的 PID-1 或由现有 smoke 入口启动。

新增动态链接 smoke：内核加载 `dyn_demo` → ld.so 自重定位 → 加载 `libdemo.so` → 绑定 `demo_add`/`demo_msg` → 调用并校验返回值 → 发出确定性 `BIGOS_DYNLINK_PASSED`/`BIGOS_DYNLINK_FAILED` marker。覆盖正常闭环与失败路径（解释器缺失、`DT_NEEDED` 库缺失、未解析符号、超界、越界基址）。默认关闭时不构建上述产物，默认启动与 `/bin/*` 静态集合不变。

- 备选：把示例做成内核内注入。否决：动态链接的价值在于真实用户态加载/重定位闭环，应走真实 VFS + ring3 路径。

## 控制流 / 数据流（动态执行路径）

```
execve/launch_init(dyn_demo)
  └─ validate_elf: ET_DYN + 恰好一个 PT_INTERP + 有界 PT_LOAD 校验
       ├─ 读取 PT_INTERP 路径 → VFS 读取 ld-bigos.so (ET_DYN)
       └─ 产出: 主镜像段[USER_DYN_EXEC_BASE], 解释器段[USER_INTERP_BASE]
  └─ map_segment(主镜像, base=USER_DYN_EXEC_BASE)
  └─ map_segment(解释器, base=USER_INTERP_BASE)
  └─ copy_exec_args_to_stack: [argc][argv][NULL][envp][NULL][auxv: AT_PHDR/PHENT/PHNUM/ENTRY/BASE/PAGESZ/AT_NULL][strings]
  └─ ring3 entry = USER_INTERP_BASE + ld.so e_entry   (而非主镜像 e_entry)

ring3 → _dl_start (ld.so):
  GOT[0]+PC-relative 求自身 bias(AT_BASE 交叉校验) → 自重定位(R_X86_64_RELATIVE)
  → 解析主镜像 PT_DYNAMIC(AT_PHDR/PHNUM)
  → 加载 DT_NEEDED(libdemo.so) 到 USER_DYN_LIB_BASE 区
  → eager 重定位(主镜像 + libdemo.so): RELATIVE/GLOB_DAT/JMP_SLOT/64 + 全局符号绑定
  → 跳转 AT_ENTRY(主镜像入口) → crt0 → main → 调用 libdemo 符号
```

失败行为：任一阶段（解释器读取失败、`ET_DYN`/`PT_INTERP` 校验失败、基址越界/冲突、auxv 构造失败、ld.so 加载库失败、符号未解析、超界）MUST 走确定性失败：内核侧发 `BIGOS_USER_ELF_*`/统一 panic 或拒绝进入 ring3；ld.so 侧发确定性诊断 marker 并经 `SYS_EXIT` 退出，绝不跳转到未重定位/未定义地址。

## 地址布局 / ABI / 链接脚本变更

- 初始栈 ABI：在 `envp` 的 NULL 终止符后追加 auxv 数组（`AT_NULL` 终止）。追加式，既有静态 crt0 透明。
- 用户 VM 布局：新增 `USER_DYN_EXEC_BASE`/`USER_INTERP_BASE`/`USER_DYN_LIB_BASE`/`USER_DYN_LIB_MAX_PAGES`（有界、低半区、不与现有区冲突），落在 `user-runtime-vm-layout` 既有预留 gap 内。
- 链接脚本：ld.so 与示例库/动态程序使用新增的 `-fPIC`/`-shared`/`-pie` 链接脚本或链接选项，产出含 `PT_DYNAMIC`（动态程序额外含 `PT_INTERP`）的镜像；不改 `user/link.lds`（静态程序）与内核 `link.lds`。
- 不改：启动地址、内核链接地址 `0xffffffff80000000`、页表自映射地址、磁盘布局、IDT/syscall vector(`0x80`)、CR3 切换、既有静态 `ET_EXEC` 装载 ABI 与既有 syscall number。

## Risks / Trade-offs

- [Risk] 初始栈 auxv 追加破坏既有静态 crt0 → Mitigation：追加式（在 envp NULL 之后），静态 crt0 只读到 envp NULL 即停止；静态路径可选择不写 auxv 或写空向量；用 smoke 同时验证静态与动态程序启动。
- [Risk] `ET_DYN`/`PT_INTERP` 放宽削弱既有装载器安全校验 → Mitigation：动态路径在默认关闭守卫内；保留所有越界/重叠/W^X/对齐/入口可执行校验，仅按基址偏移调整地址校验；静态 `ET_EXEC` 路径零改动。
- [Risk] ld.so 自重定位顺序错误（在重定位完成前访问全局量）导致崩溃 → Mitigation：`_dl_start` 早期阶段仅用 PC-relative/栈局部量与裸 `R_X86_64_RELATIVE` 处理，自重定位完成前不调用依赖 GOT 的函数；以小型可审查汇编/`-fno-plt` 约束实现。
- [Risk] 重定位/符号解析子集不完整导致示例库无法绑定 → Mitigation：示例库刻意只用受支持重定位类型（`-fPIC` + eager + 无 TLS/IFUNC）；在 spec 明确支持的重定位类型与非目标；构建期校验示例库重定位类型在子集内。
- [Risk] 工具链不支持所需 `-fPIC`/`-shared`/`-pie` 或生成超子集重定位（如 TLS/IFUNC） → Mitigation：构建期检查交叉工具链能力；若不可用记录 blocker 与替代检查；示例代码避免触发 TLS/IFUNC。
- [Risk] 共享对象映射区与 file-backed/匿名区冲突或越界 → Mitigation：新增有界基址常量纳入 `user-runtime-vm-layout` 校验与 VMA 集合，超界确定性拒绝并 safe-release。
- [Risk] QEMU/Bochs 仅单核基线即可，但 smoke 真实加载路径依赖磁盘镜像打包 ld.so/库 → Mitigation：默认关闭开关下才打包；smoke 走真实 VFS 读取，验证可在标准 QEMU headless 运行；不可用时记录跳过与剩余风险。

## Migration Plan

1. 在 `include/bigos/elf.h` 确认/补齐 auxv（`AT_*`、`Elf64_auxv_t`）与重定位类型（`R_X86_64_*` 子集、`ELF64_R_TYPE/SYM`）常量；在 `include/bigos/proc.h` 新增有界动态布局常量与上限。
2. 扩展内核 `validate_elf`：在默认关闭守卫内识别 `ET_DYN` + 单 `PT_INTERP`，产出主镜像 + 解释器两组有界段描述，保留全部既有安全校验。
3. 扩展 `map_segment`/段映射：支持按确定性基址偏移映射 `ET_DYN` 段；解释器经 VFS 读取并加载。
4. 扩展 `copy_exec_args_to_stack`：在 envp NULL 后追加有界 auxv；静态路径保持透明。
5. 在 `execve`/`launch_init` 动态路径选择解释器入口为 ring3 entry，并把布局纳入 `user-runtime-vm-layout` 描述与 VMA 集合。
6. 新增用户态 ld.so（`_dl_start` + 自重定位 + `DT_*` 解析 + 有界重定位/符号绑定子集 + 移交入口），freestanding-safe。
7. 扩展构建管线：默认关闭开关下构建 ld.so、`libdemo.so`、`dyn_demo`，打包到约定路径；不改默认 `/bin/*` 静态集合。
8. 新增动态链接 smoke 入口与 marker，覆盖正常闭环与失败路径；默认启动回归确认不依赖动态组件。
9. 更新 docs/en 与 docs/zh 镜像，描述有界动态链接边界（不声称完整 POSIX 动态链接器/`dlopen`/TLS/版本符号）。
10. 实现验证完成后更新 `roadmap.md` 中 M12.1 完成状态，保持 roadmap 仅项目规划级描述。

Rollback：关闭 `dynamic_link_smoke` 即不构建/打包任何动态组件、不构造 auxv 之外的动态行为；移除动态装载守卫分支即回到"仅静态 `ET_EXEC`"状态。auxv 追加与新增布局常量是追加式，回滚需同步从内核初始栈构造与布局常量中移除。由于不改既有静态 ABI、syscall number、boot/页表/磁盘布局，回滚不需要既有静态程序迁移。

## Open Questions

- 已定（见决策 4）：ld.so 构建以 `-fPIC -Bsymbolic -fvisibility=hidden` 为核心、产出 `ET_DYN`（`-shared -e _dl_start` 或 `-pie` 均可，按交叉工具链产出择定）；自身基址定位采用 GOT[0] + PC-relative 标准技巧，`AT_BASE` 仅作交叉校验。不影响 spec 行为边界。
- 已定（见决策 3）：主镜像 program header 不强依赖显式 `PT_PHDR`；`AT_PHDR` 采用扫描 `PT_LOAD` 找覆盖 `e_phoff` 段的算法，并在 `validate_elf` 强制 "phdr 区间须完整落在某只读 `PT_LOAD` 内，否则确定性拒绝"。属实现/装载细节，不改变 auxv 契约。
- 无其余未决项。
