## 验证记录

### 已通过

- `openspec validate add-writable-directory-tree --strict`：通过。
- `xmake`：通过。
- `uv run pytest tests/test_syscall_entry_source.py tests/test_writable_fs_page_cache_pipe_source.py -q`：通过，`26 passed`。
- `clang++ -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -target x86_64-elf -nostdinc++ -Iinclude -Icpp/include -Icpp/libsupc++/include -Ikernel -I. -DBIGOS_USER_PROCESS -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc kernel/core/proc/proc.cc kernel/core/kernel.cc`：通过。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/writable-dir-tree-serial.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED`：通过，观测到 `BIGOS_WRITABLE_FS_PASSED`。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/filesystem-maturity-dir-tree-serial.log --expect-serial-marker BIGOS_FILESYSTEM_MATURITY_PASSED --smoke-timeout 80`：通过，观测到 `BIGOS_FILESYSTEM_MATURITY_PASSED`。该路径覆盖 shell 中 `cd /rw` 后的相对目录树操作、`rmdir` 非空错误、`ls`/`stat` 可观察性、删除后 `stat` 的 `ENOENT`、deleted-directory cwd 行为和 shell 存活性。
- persistent clean-sync 双阶段验证：
  - 写入阶段：`uv run python tools/boot_debug.py run --emulator qemu --display none --persistent-image build/test/persistent-dir-tree.raw --serial-log build/test/persistent-dir-tree-write-serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`：通过。
  - 验证阶段：`uv run python tools/boot_debug.py run --emulator qemu --display none --persistent-image build/test/persistent-dir-tree.raw --serial-log build/test/persistent-dir-tree-verify-serial.log --expect-serial-marker BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`：通过。
- `uv run python tools/boot_debug.py run --emulator bochs --display none --serial-log build/test/bochs-dir-tree-serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`：通过，观测到 `BIGOS_USER_EXEC`。
- 定向搜索 `rg "Stage [0-9]|阶段 [0-9]|Stage[0-9]|阶段[0-9]" openspec/changes/add-writable-directory-tree docs/en docs/zh include/bigos/fs kernel/core/fs kernel/core/syscall kernel/core/proc user tests tools xmake -n`：无匹配。

### 已运行但未通过

- `uv run pytest tests/test_fd_vfs_shell_source.py ...` 的组合运行中，`tests/test_fd_vfs_shell_source.py` 仍引用已归档/不存在的 `openspec/changes/harden-runtime-filesystem-semantics/runtime-filesystem-semantics.md`，触发 `FileNotFoundError`。该问题与本变更新增目录树实现无直接关系，未在本变更中修复。
- `/opt/homebrew/opt/llvm/bin/clangd --check=kernel/core/fs/bigfs.cc --compile-commands-dir=.`：clangd 完成 AST 构建，但 check 模式的 ExtractFunction tweak 报 `Cannot extract break/continue without corresponding loop/switch statement` 并以退出码 3 结束；未观测到本变更新增源码语法错误。

### 残余风险

- deleted-directory cwd 当前以进程内 `cwd_deleted` 标记保证相对 lookup 不会静默指向新同名目录，`getcwd` 返回保存路径，`chdir("..")` 可脱离 deleted 状态；目录 inode/data 的 cwd 引用级精确 pin 仍是后续可加强点。
- userland filesystem maturity smoke 已通过；shell transcript 为降低进程对象消耗，保留目录树核心 path-tool 覆盖，移除了部分与本变更重复度较低的旧 shell 子场景。
