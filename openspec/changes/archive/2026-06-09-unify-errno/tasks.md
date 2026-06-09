## 1. 盘点现状

- [x] 1.1 全量检索现有错误码定义与引用点（`SYS_E*`、`FD_E*`、`WAIT_E*` 及裸负值），
  确认 `EBADF=-9`、`EWOULDBLOCK=-11`、`EFAULT=-14`、`EINVAL=-22`、`EMFILE=-24`、
  `ENOSYS=-38`、`ECHILD=-10` 的完整使用清单
- [x] 1.2 确认 `sched` 的 `WAIT_TIMEOUT`/`WAIT_BLOCK_FORBIDDEN` 与 driver `BlockStatus`
  为收敛范围之外，记录边界

## 2. 新增单一错误码来源

- [x] 2.1 新增 `include/bigos/errno.h`，在 `bigos` 命名空间集中定义正值 errno 常量
  （`EBADF=9`、`EWOULDBLOCK=11`、`EFAULT=14`、`EINVAL=22`、`EMFILE=24`、`ENOSYS=38`、
  `ECHILD=10`），保持 freestanding-safe 与最小依赖
- [x] 2.2 确认头文件 include guard、命名空间宏与现有约定一致

## 3. 收敛子系统错误码引用

- [x] 3.1 修改 `include/bigos/syscall.h`：`SYS_E*` 改为引用 `bigos/errno.h` 的取负
  别名，删除独立数值定义，保证数值不变
- [x] 3.2 修改 `include/bigos/proc.h`：`FD_E*`、`WAIT_E*` 改为引用 `bigos/errno.h`
  的取负别名，删除独立数值定义，保证数值不变
- [x] 3.3 更新 `src/kernel/syscall/syscall.cc` 等引用点改用统一错误码符号
- [x] 3.4 更新 `src/kernel/proc/proc.cc`、`src/kernel/fs/` 等引用点改用统一错误码符号
- [x] 3.5 复查并移除残留的冗余前缀别名（如同值的 `SYS_EBADF` 与 `FD_EBADF` 不再
  各自独立定义）

## 4. 文档与测试同步

- [x] 4.1 更新 `docs/en/arch/syscall-entry.md`、`docs/en/arch/fd-vfs-shell.md` 中对
  错误码符号名的描述，并同步 `docs/zh` 对应镜像
- [x] 4.2 更新 `tests/test_syscall_entry_source.py`、`tests/test_fd_vfs_shell_source.py`
  及其他断言错误码符号名的 source-contract 测试

## 5. 验证

- [x] 5.1 运行 `xmake` 构建，确认无残留重复定义、无编译错误
- [x] 5.2 运行 `uv run pytest`（至少受影响的 source-contract 测试）确认通过
- [x] 5.3 复核错误码返回寄存器数值未变（必要时 QEMU headless smoke 复核 syscall 返回
  路径），记录验证结果或显式记录跳过原因与残留风险
- [x] 5.4 运行 `openspec validate unify-errno` 确认 change 结构合法
