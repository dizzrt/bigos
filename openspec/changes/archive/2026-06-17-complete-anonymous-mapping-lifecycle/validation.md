## 验证记录

### 地址与入口边界审查

- 未修改 Legacy BIOS/MBR 启动地址、linker 地址、higher-half kernel base、direct map window、`KVMEM_BASE`、recursive self-mapping base、`VECTOR_SYSCALL = 0x80`、IDT DPL 策略或 exception/IRQ/syscall 的 EOI 分离规则。
- 新增 `SYS_UNMAP_ANON = 36` 与 `SYS_PROTECT_ANON = 37` 只追加 syscall number，不重排既有 ABI；dispatch 仍通过 `int 0x80` 的 `InterruptFrame` 寄存器约定返回确定性负 errno。
- 页表改动只新增用户低半区 leaf unmap helper 与动态 owned 用户页表空表回收；static kernel page table、higher-half、direct map、KVMEM 与 self-mapping 条目不释放。

### 已通过检查

- `openspec validate complete-anonymous-mapping-lifecycle --strict`：通过。
- `uv run pytest tests/test_vma_user_memory_api_source.py tests/test_syscall_entry_source.py`：27 passed。
- `uv run pytest tests/test_user_c_baseline_source.py::test_smoke_probe_programs_are_packaged_only_for_userland_smoke tests/test_user_c_baseline_source.py::test_user_libc_exposes_bounded_fine_grained_headers`：2 passed。
- `xmake f --anonymous_lifecycle_smoke=y`：通过。
- `xmake`：通过。
- `xmake build user-init-elf`：通过。
- `clang++ --target=x86_64-elf ... -fsyntax-only kernel/core/proc/proc.cc`：通过。
- `clang++ --target=x86_64-elf ... -fsyntax-only kernel/mm/vmem.cc`：通过。
- `clang++ --target=x86_64-elf ... -fsyntax-only kernel/core/syscall/syscall.cc`：通过。
- `x86_64-elf-gcc ... -std=c17 -Wall -Wextra -c user/smoke/anonymous_lifecycle_smoke.c`：通过。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/anon-lifecycle.serial.log --expect-serial-marker BIGOS_ANON_LIFECYCLE_PASSED --smoke-timeout 60`：通过，串口观测到 `BIGOS_ANON_LIFECYCLE_PASSED`，并在 marker 前观测到两次 `BIGOS_USER_PAGE_FAULT`，覆盖 access-after-unmap 与 write-after-readonly 子进程故障路径。

### 已知非本 change 阻塞或噪声

- `clangd --check=kernel/core/proc/proc.cc --compile-commands-dir=. --log=error`、`clangd --check=kernel/mm/vmem.cc --compile-commands-dir=. --log=error`、`clangd --check=kernel/core/syscall/syscall.cc --compile-commands-dir=. --log=error` 均以 exit code 3 退出；输出为 clangd tweak / IncludeCleaner 内部诊断，例如 `ExtractFunction ==> FAIL`、`SwapBinaryOperands ==> FAIL`、`AddUsing ==> FAIL`，未报告当前 change 引入的 C++ 语义诊断。替代检查为上述 `clang++ -fsyntax-only` 与 `xmake` cross-toolchain build。
- `uv run pytest tests/test_vma_user_memory_api_source.py tests/test_syscall_entry_source.py tests/test_user_c_baseline_source.py` 中 `tests/test_user_c_baseline_source.py` 仍有既有源码断言与当前文件不一致：`user/bin/ls.c` 不再包含旧的 `bigos_readdir(fd, entries, BIGOS_DIRENT_MAX_BATCH)` 字符串，`user/smoke/bin/libc_subset.c` 的 stderr 格式串包含额外字段。这些失败与本 change 的匿名映射生命周期实现无关；本 change 新增/触及的 user libc header 与 packaging 断言已单独通过。

### 未运行检查

- 未运行 Bochs 交叉验证；本次已通过 QEMU headless 串口 marker smoke，剩余 Bochs 风险为本机 ROM/display/image path 与运行时环境差异。
- 未运行 Python `ruff`、`pyright`、全量 `pytest`，因为本 change 未新增或修改 Python helper；Python 相关检查不适用。
