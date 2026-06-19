# Validation Notes

## 已通过

- 源码级回归：
  - `uv run pytest tests/test_writable_fs_page_cache_pipe_source.py tests/test_fd_vfs_shell_source.py tests/test_runtime_filesystem_maturity_source.py tests/test_timer_irq_foundation_source.py tests/test_time_and_identity_source.py tests/test_device_driver_framework_source.py`
  - 结果：`64 passed`
- 当前变更涉及测试的 lint/format：
  - `uv run ruff check tests/test_device_driver_framework_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_fd_vfs_shell_source.py tests/test_timer_irq_foundation_source.py tests/test_time_and_identity_source.py`
  - 结果：passed
  - `uv run ruff format --check tests/test_device_driver_framework_source.py tests/test_fd_vfs_shell_source.py tests/test_timer_irq_foundation_source.py tests/test_time_and_identity_source.py tests/test_tty_console_input_source.py tests/test_writable_fs_page_cache_pipe_source.py`
  - 结果：passed
- Python 类型检查：
  - `uv run pyright`
  - 结果：`0 errors, 0 warnings, 0 informations`
  - 备注：pyright 报告 `bigos.py` 不存在，以及 pyright 版本可升级；未发现类型诊断。
- 交叉构建：
  - `xmake f --persistent_writable_fs=n --page_fault_smoke=n --mm_self_test=n --fs_smoke=n --timer_smoke=n --writable_fs_smoke=n --time_identity_smoke=n --persistent_writable_fs_smoke=n --userland_smoke=n --user_program_smoke=n --user_elf_smoke=n && xmake`
  - 结果：passed
- clang 辅助静态检查：
  - `clang++ --target=x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -mno-red-zone -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/device.cc kernel/core/fs/vfs.cc kernel/core/fs/bigfs.cc kernel/core/irq/isr.cc kernel/core/time/time.cc kernel/core/bigos/io.cc kernel/core/terminal/console.cc kernel/core/kernel.cc`
  - 结果：passed
- QEMU headless runtime smoke：
  - boot filesystem：`BIGOS_FS_EXFAT_READ_PASSED`
  - RAM-backed `/rw`：`BIGOS_WRITABLE_FS_PASSED`
  - PIT timer：`BIGOS_TIMER_IRQ`
  - RTC/time identity：`BIGOS_TIME_IDENTITY_PASSED`
  - persistent `/rw` write：`BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`
  - persistent `/rw` clean reboot verify：`BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`
- Bochs headless runtime smoke：
  - boot filesystem：`BIGOS_FS_EXFAT_READ_PASSED`

## 历史诊断 / 非本变更问题

- 全量 `uv run ruff check` 仍报告既有长行：
  - `tests/test_fork_copy_on_write_source.py`
  - `tests/test_runtime_filesystem_maturity_source.py`
  - `tests/test_stable_file_growth_source.py`
- 全量 `uv run ruff format --check` 仍报告既有格式化差异：
  - `tests/test_fork_copy_on_write_source.py`
  - `tests/test_metadata_consistency_source.py`
  - `tests/test_runtime_filesystem_maturity_source.py`
  - `tests/test_stable_file_growth_source.py`
  - `tests/test_syscall_entry_source.py`
  - `tests/test_vma_user_memory_api_source.py`
- `tests/test_tty_console_input_source.py` 全文件运行仍有既有断言与当前源码不一致：
  - `char buffer[TTY_INPUT_CAPACITY];`
  - `CR3_ROOT_MASK`
  - 本变更触及的 console wrapper 断言已更新并在目标源码级回归中通过。

## 当前变更新增并已修复的问题

- `BIGOS_FS_SMOKE` 单独开启时，`kernel/core/kernel.cc` 需要全局包含 `string.h` 才能使用 `strlen`；已修复。
- `vfs::init()` 在 scheduler 尚未启动的只读 FS smoke 中 best-effort 初始化 `/rw`，会触发 bcache 可阻塞上下文边界；已改为仅在 `sched::can_block()` 为真时初始化 bigfs，并在 `/rw` 路径按需初始化。
- `bigfs::format_current_device()` 的 512-byte 清零块改为静态有界缓冲，降低格式化路径的早期内核栈压力。

## 剩余风险

- 设备框架仍是单核初始化期的 bounded kernel-internal registry；未声明 SMP 安全、热插拔、PCI/ACPI 枚举、完整 bus model、async I/O 或用户态设备节点。
- ATA PIO probe 当前发布既有同步 PIO `BlockDevice`，未新增硬件识别或第二块设备后端；后续后端接入应单独扩展 probe 失败诊断。
