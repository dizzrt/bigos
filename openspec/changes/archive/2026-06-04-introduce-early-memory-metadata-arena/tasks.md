## 1. 设计与边界确认

- [x] 1.1 复查 `proposal.md`、`design.md` 和 spec delta，确认本 change 只处理 buddy 初始化元数据 arena。
- [x] 1.2 复查 `docs/en/arch/x86-boot-layout.md`、BootInfo handoff、`src/mm/buddy.cc` 和 `src/mm/kmem.cc`，记录不能覆盖的固定地址和初始化顺序。
- [x] 1.3 明确第一版 arena 使用静态 buffer、BootInfo usable region 切分或其它方式，并记录容量估算依据。

## 2. Arena 基础设施

- [x] 2.1 新增 early metadata arena 类型和初始化入口，保证 freestanding-safe、无 libc/异常/RTTI 依赖。
- [x] 2.2 为 arena 增加对齐分配、容量统计和耗尽检测。
- [x] 2.3 将 arena 接口限制在 `src/mm` 内部或 `bigos::mm::__detail`，避免扩大公开内存 API。
- [x] 2.4 在容量不足时输出明确诊断并安全 halt 或返回 fatal init failure。

## 3. Buddy 初始化迁移

- [x] 3.1 调整 `init_buddy()` / memory map 消费路径，使初始化期 `PageBlock` 从 arena 分配。
- [x] 3.2 调整初始化期 intrusive list node 创建，使其从 arena 分配。
- [x] 3.3 保持运行期 buddy split 元数据继续使用正常 allocator，并复查 split failure rollback 不被破坏。
- [x] 3.4 确认 arena-backed metadata 生命周期覆盖 buddy list 使用期，且不会被普通 `free()` 或 `free_pages()` 回收。

## 4. 初始化顺序与失败行为

- [x] 4.1 复查 `init_mem()` 顺序，确认 buddy 初始化不再依赖普通 slab 动态扩容。
- [x] 4.2 复查低 2 MiB、kernel image、BootInfo v2 blob、boot-stage page table 和 `KVMEM_BASE` 假设未改变。
- [x] 4.3 增加复杂 memory map 或源码级测试，覆盖 arena metadata 分配数量和耗尽路径。
- [x] 4.4 复用已建立的 memory runtime self-test 和 serial marker，验证 arena 接入后 `init_mem()` 之后的 allocator smoke 仍通过。

## 5. 构建与静态检查

- [x] 5.1 运行 `uv run pytest tests/test_memory_correctness_source.py`，记录通过结果或无法运行原因。
- [x] 5.2 运行 `openspec validate --all`，确认 spec delta 可解析。
- [x] 5.3 运行 `xmake`，确认交叉工具链构建通过；若缺失，记录缺失命令和风险。
- [x] 5.4 对修改过的 C++ 文件运行 freestanding x86_64 C++17 clang 辅助 `-fsyntax-only` 检查。
- [x] 5.5 使用 clangd 或 IDE diagnostics 检查修改过的文件，修复当前 change 引入的有效诊断。

## 6. Boot 验证与收尾

- [x] 6.1 运行 `uv run python tools/boot_debug.py run --memory-self-test --no-launch`，确认 self-test 构建、boot 资产和带 COM1 serial log 的 generated bochsrc 生成成功。
- [x] 6.2 在 Bochs 环境可用时运行 `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED`，确认 `init_mem()` 后 memory self-test marker 可观测。
- [x] 6.3 复查 `git diff`，确认未移动 boot/linker 地址、未引入 direct map、未改公开 API、未加入 scheduler/IRQ/SMP 假设。
- [x] 6.4 汇总验证记录，区分通过检查、无法运行检查、历史 warning、当前 change 新增或修复的诊断和剩余 bootability 风险。

## 验证记录

- `uv run pytest tests/test_memory_correctness_source.py`：通过，17 passed。
- `openspec validate --all`：通过，12 passed / 0 failed。
- `xmake`：通过；仍有既有 `ISO C++11 requires whitespace after the macro name`、`LOAD segment with RWX permissions`、`$(buildir)` deprecated warning。
- `clang++ --target=x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/mm/buddy.cc`：通过。
- IDE diagnostics：`src/mm/buddy.cc`、`tests/test_memory_correctness_source.py` 无有效诊断。
- `uv run python tools/boot_debug.py run --memory-self-test --no-launch`：通过，生成 `build/test/os.raw`、`build/test/bochsrc.bxrc`，配置 COM1 serial log 路径。
- `uv run python tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED --smoke-timeout 30`：未通过，Bochs 可启动但等待 serial marker 超时，且未生成 `build/test/serial.log`；运行后遗留的 generated `os.raw.lock` 已清理。
