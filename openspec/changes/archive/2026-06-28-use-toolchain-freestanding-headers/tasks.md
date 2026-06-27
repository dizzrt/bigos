## 1. 盘点与确认

- [x] 1.1 确认 `include/stddef.h`、`include/stdint.h`、`include/stdarg.h` 三份均为 GCC vendor 副本（版权头/guard），并列出其全部消费者（`include/bigos/types.h`、`include/bigos/io.h`、`cpp/include/ktl/pair.h`、`kernel/core/bigos/io.cc`、`kernel/core/bigos/panic.cc`、`kernel/mm/slab.cc` 等）。
- [x] 1.2 确认内核/ C++ 支持库构建（`xmake/kernel.lua`、`xmake/common.lua`）使用 `-ffreestanding` 且不带 `-nostdinc`，工具链 freestanding 头在默认搜索路径；记录若某配置带 `-nostdinc` 的回退方案（`-isystem $(x86_64-elf-gcc -print-file-name=include)`）。
- [x] 1.3 确认变更范围严格限定这三份头，不涉及 `include/string.h` 等其它 `include/` 头，也不涉及用户态 `user/**`。

## 2. ABI 护栏先行

- [x] 2.1 在 `include/bigos/types.h`（已包含 `<stdint.h>`/`<stddef.h>` 且被广泛包含）加入编译期 LP64 x86_64 断言：`sizeof(size_t)==8`、`sizeof(long)==8`、`sizeof(uint64_t)==8 && sizeof(uint32_t)==4`、`__CHAR_BIT__==8`，附描述性失败信息。
- [x] 2.2 在删除副本前先构建一次，确认断言在当前工具链通过、零运行时成本。

## 3. 删除 vendor 副本并切换来源

- [x] 3.1 删除 `include/stddef.h`、`include/stdint.h`、`include/stdarg.h`。
- [x] 3.2 不修改任何 `#include <stddef.h>`/`<stdint.h>`/`<stdarg.h>` 写法；确认删除后这些 include 回落到工具链 freestanding 头。

## 4. 构建与验证

- [x] 4.1 运行 `xmake`/`xmake build kernel` 完整内核 + C++ 支持库构建（`x86_64-elf-gcc`/`g++`/`ld`/`as`），确认删除副本后编译链接通过；若交叉工具链/构建环境不可用则记录 blocker、替代检查与剩余风险。
- [x] 4.2 运行接近 GCC 交叉构建环境的 clang 辅助检查（freestanding C++17、`-target x86_64-elf`、`-mcmodel=kernel` 等、no exceptions/RTTI、项目 include）覆盖受影响的内核 C++ 源（`io.cc`/`panic.cc`/`slab.cc` 与 `types.h`/`io.h` 消费者）；修复当前变更新增有效诊断，历史诊断与 freestanding false positive 分开记录。
- [x] 4.3 运行对应 clangd 辅助诊断或记录 clangd flags/config 差距；修复当前变更新增有效诊断。
- [x] 4.4 跑默认启动回归（优先 QEMU `--display none` headless，观察 `BIGOS_USER_EXEC`；可用时 Bochs Legacy BIOS 交叉验证），确认变参 panic/日志路径（依赖 `<stdarg.h>`）与整体运行时行为不变；环境不可用时记录跳过原因与剩余风险。
- [x] 4.5 若涉及任何 Python host-side 辅助改动，使用 `uv run ...` 并补 `uv run ruff check`/`ruff format --check`/`pyright`/`pytest`；若未改 Python，记录不适用。

## 5. 文档与收尾

- [x] 5.1 若 docs/en 与 docs/zh 中描述了 `include/` 下 freestanding C 头子集，更新为「这三份标准 C 头由交叉工具链提供、仓库不再 vendor」，保持目录结构同构与仓库相对路径；若文档未涉及则记录不适用。
- [x] 5.2 运行 `openspec validate use-toolchain-freestanding-headers --strict` 与 `openspec status`，确认 artifacts/规格/任务处于可归档状态；验证记录区分已通过、无法运行（含原因与剩余风险）、历史诊断与当前变更新问题。

## 验证记录

### 已通过

- **删除前 ABI 护栏构建（任务 2.2）**：`xmake build kernel`（`x86_64-elf-g++ 12.2.0`）在三份 vendor 副本仍在场、`types.h` 已加 `static_assert` 的情况下编译通过，证明 LP64 断言在当前工具链成立且为编译期、零运行时成本。
- **删除后完整内核+ C++ 支持库构建（任务 4.1）**：删除 `include/{stddef,stdint,stdarg}.h` 后 `xmake clean kernel && xmake build kernel` 完整编译 + `x86_64-elf-ld` 链接通过，产出 `build/kernel`（ELF 64-bit x86-64 静态可执行）。`#include <stddef.h>/<stdint.h>/<stdarg.h>` 均回落到交叉工具链 freestanding 头，写法零改动。
- **clang 辅助检查（任务 4.2）**：以 `-target x86_64-elf -ffreestanding -std=c++17 -mcmodel=kernel -mno-red-zone -mno-sse/-sse2/-mmx -fno-rtti -fno-exceptions -nostdinc++` 并 `-isystem $(x86_64-elf-gcc -print-file-name=include)` 覆盖 `kernel/core/bigos/io.cc`、`kernel/core/bigos/panic.cc`、`kernel/mm/slab.cc`（间接覆盖 `include/bigos/types.h`、`include/bigos/io.h` 消费者）：0 错误。
- **默认启动回归（任务 4.4）**：`uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial-freestanding-headers.log --expect-serial-marker BIGOS_USER_EXEC` 观测到 `BIGOS_USER_EXEC`，串口日志干净到达 shell 提示符、`BIGOS_PANIC` 0 次，证明依赖 `<stdarg.h>` 的变参日志/panic 路径与整体运行时行为不变。该次运行同时构建了 UEFI loader（LLP64 `-target x86_64-pc-win32`）与内核（LP64），均通过。
- **OpenSpec 收尾（任务 5.2）**：`openspec validate use-toolchain-freestanding-headers --strict` 返回 valid；`openspec status` 显示 proposal/design/specs/tasks 四件 artifact 均 done、`isComplete: true`，处于可归档状态。

### 无法运行 / 剩余风险

- **Bochs Legacy BIOS 交叉验证（任务 4.4 可选项）**：未运行 Bochs 交叉验证；默认启动回归已通过 QEMU headless 覆盖，且本变更不触碰启动地址/链接地址/磁盘布局/IDT 等早期低层路径，剩余风险低。
- **文档同步（任务 5.1）**：不适用。`docs/en` 与 `docs/zh` 仅描述用户态 freestanding 头策略（`docs/{en,zh}/arch/userland-runtime.md`，本就声明工具链提供、仓库不 vendor），不存在描述内核 `include/` 下标准 C freestanding 头子集的段落，无需更新；docs 目录结构保持同构。
- **clangd 静态配置差距（任务 4.3）**：`.clangd` 内核段未加 GCC freestanding `-isystem`，clangd 会落到 clang 自带 resource-dir `stddef.h`，对受影响源报 8 条 `nullptr_t`/KTL `_List_node` 误报；补 `-isystem $(x86_64-elf-gcc -print-file-name=include)` 后归零。未在 `.clangd` 写死该路径，因其形如 `.../gcc/x86_64-elf/12.2.0/include` 为机器/版本相关绝对路径（AGENTS.md 禁止提交机器相关绝对路径），且 clangd 静态配置无法执行命令动态解析。该差距为编辑器侧 IDE 体验问题，不影响权威 GCC 构建与运行时；记录为已知 flags/config 差距。

### 历史诊断（与本变更无关，pre-existing）

- **clang vs GCC freestanding 头分歧**：在不提供 GCC `-isystem` 时，clang 用自身 resource-dir `stddef.h`（`nullptr_t` 暴露方式与 GCC freestanding 头不同），级联触发 `cpp/include/ext/aligned_buffer.h` 的 `nullptr_t` 未知类型与 `cpp/include/ktl/list.h` 的 `_List_node` static_cast 报错。已对照确认：在三份 vendor 副本在场时同样的 clang 命令亦为该现象（副本在场时报 0 错误是因副本顶替了 clang resource-dir 头），属 clang/GCC 工具链头差异的既有现象，非本变更引入；权威 GCC 构建与运行时均不受影响。

### 本变更新增问题与处置

- **ABI 护栏暴露 UEFI loader 的 LLP64 真实约束**：初版护栏把 `sizeof(long)==8` 设为无条件断言，被 `kernel/arch/x86/uefi/uefi.h` → `include/bigos/types.h` 间接包含的 UEFI loader（`clang -target x86_64-pc-win32`，Windows x64 LLP64，`long` 为 4 字节）在编译期当场失败——正是护栏的预期可观察行为，且暴露出 `types.h` 并非仅 LP64 内核可见。处置：将 `sizeof(long)==8` 用 `#if defined(__LP64__) || defined(_LP64)` 收口，仅在 LP64 数据模型下断言；`size_t==8`、定宽类型宽度、`__CHAR_BIT__==8` 三条在任意 x86_64 ABI 均成立，保持无条件。内核 LP64 构建四条断言全部触发并通过，LLP64 UEFI shim 正确跳过 `long` 专项断言并通过。契约（决策 3）不变，仅为放置点微调（design Open Questions 已预留该自由度）。
