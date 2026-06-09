## Context

BigOS 当前错误码按子系统各自定义，且数值重复：

- `include/bigos/syscall.h`：`SYS_ENOSYS=-38`、`SYS_EFAULT=-14`、`SYS_EBADF=-9`、
  `SYS_EINVAL=-22`、`SYS_EMFILE=-24`、`SYS_EWOULDBLOCK=-11`。
- `include/bigos/proc.h`：`WAIT_ECHILD=-10`、`WAIT_EINVAL=-22`、
  `WAIT_EWOULDBLOCK=-11`、`FD_EBADF=-9`、`FD_EMFILE=-24`、`FD_EWOULDBLOCK=-11`。

可见 `EBADF`(-9)、`EWOULDBLOCK`(-11)、`EINVAL`(-22)、`EMFILE`(-24) 在多个子系统重复
定义同一数值，仅前缀不同。这些都是 POSIX 风格 errno 取负后写入返回寄存器（`rax`）
的负值。

约束：

- freestanding 环境，无 libc、无用户态 `errno` 全局变量；错误码必须是编译期整型常量。
- 返回寄存器中的负值数值是既有 ABI 的一部分，本次收敛**不得改变任何数值**。
- 项目命名约定：内核 API 在 `bigos` 命名空间下；公共头文件保持最小。
- `src/kernel/sched/`、`src/drivers/block/` 另有独立的错误表达（`WAIT_TIMEOUT=-110`、
  `BlockStatus` 枚举），它们语义独立，不属于本次 POSIX errno 收敛范围。

## Goals / Non-Goals

**Goals:**

- 建立单一错误码来源 `include/bigos/errno.h`，集中定义当前在用的 POSIX 风格错误码。
- 消除 `SYS_E*`、`FD_E*`、`WAIT_E*` 之间的重复定义，让它们统一引用单一来源。
- 保持所有错误码数值与可观察行为（返回寄存器中的负值）完全不变。
- 更新引用点、文档与 source-contract 测试以匹配收敛后的符号名。

**Non-Goals:**

- 不实现完整 POSIX errno 全集，只收敛当前已使用的值。
- 不引入用户态 `errno` 全局变量、`strerror` 或错误码到字符串映射。
- 不改动 `sched` 的 `WAIT_TIMEOUT`/`WAIT_BLOCK_FORBIDDEN` 与 driver `BlockStatus`
  枚举，它们不是 POSIX errno 语义。
- 不收敛 `EXEC_FAILURE_STATUS=-126` 等进程退出码，它是进程退出状态而非 syscall
  errno，语义不同，留待后续阶段评估。
- 不修改 syscall ABI、返回寄存器约定或任何数值。

## Decisions

### 决策 1：错误码以正值定义，调用方按惯例取负

`bigos/errno.h` 定义 POSIX 习惯的**正值** errno（如 `EBADF=9`、`EINVAL=22`），
调用方在写入返回寄存器时取负（`-EBADF`）。

理由：与 POSIX/Linux 约定一致，便于未来 libc/userland 复用同一组数值；正值定义是
单一事实来源，负值仅是 ABI 表达。

备选：直接定义负值常量（如现状 `SYS_EBADF=-9`）。否决，因为它把 ABI 表达硬编码进
错误码本体，未来引入用户态 `errno`（正值语义）会再次产生双重定义。

为降低大改动风险，可在 `errno.h` 中**同时**提供取负后的便捷常量（例如
`constexpr int64_t E_NEG_BADF = -EBADF;`）或保留各子系统现有符号名但改为引用统一
来源的别名（见决策 3）。最终采用：定义正值 errno + 在子系统头文件中将旧符号改写为
引用统一来源的 `constexpr` 别名，确保数值不变、改动可控。

### 决策 2：放置位置为 `include/bigos/errno.h`

新增公共头 `include/bigos/errno.h`，置于 `bigos` 命名空间，仅包含错误码常量，不引入
其他依赖，符合「公共头文件保持最小」约定。

### 决策 3：以别名收敛，分阶段去重而非一次性重命名全部引用点

为控制风险，先在 `syscall.h`/`proc.h` 中把现有 `SYS_E*`/`FD_E*`/`WAIT_E*` 定义改为
引用 `bigos/errno.h` 的单一来源（取负别名），保证数值不变；随后在实现文件中逐步把
引用改为统一符号，并删除冗余别名。

理由：避免一次性大范围重命名带来的编译/测试断裂，保持每一步可独立验证（编译 +
source-contract 测试）。

备选：一次性删除所有旧符号并全局替换。否决，改动面大、回滚成本高，且与「纯机械、
零语义风险」目标相悖。

### 决策 4：错误码取值沿用现状数值

`EBADF=9`、`EWOULDBLOCK=11`、`EFAULT=14`、`EINVAL=22`、`EMFILE=24`、`ENOSYS=38`、
`ECHILD=10`（均为正值，取负即现有负码）。与 Linux x86_64 errno 数值一致，未来扩展
无缝。

## Risks / Trade-offs

- [遗漏某个错误码引用点，导致编译失败或残留重复定义] → 用 Grep 全量检索
  `SYS_E*`/`FD_E*`/`WAIT_E*` 与裸数值引用点，逐一替换；以 xmake 构建验证无残留。
- [意外改变了某个错误码的数值，破坏 ABI] → 在 spec 与 tasks 中固定「数值不变」为
  硬约束；通过 source-contract 测试与构建确认返回值不变。
- [误把 `sched`/`BlockStatus` 等非 POSIX 错误表达卷入收敛] → 在 Non-Goals 明确边界，
  仅收敛 POSIX 风格 errno。
- [文档与测试未同步更新，出现描述漂移] → tasks 中包含 `docs/en`+`docs/zh` 镜像更新
  与受影响 source-contract 测试更新，并用 `uv run pytest` 验证。

## Migration Plan

1. 新增 `include/bigos/errno.h`，定义正值 errno 常量。
2. 在 `syscall.h`/`proc.h` 中将旧错误码符号改为引用统一来源的别名（数值不变）。
3. 逐步将实现文件（`syscall.cc`、`proc.cc` 等）改用统一符号，删除冗余别名。
4. 同步更新 source-contract 测试与 `docs/en`/`docs/zh` 文档。
5. 验证：xmake 构建通过；`uv run pytest` 通过；必要时 QEMU headless smoke 复核 syscall
   返回路径行为不变。

回滚：本变更为纯符号收敛、数值不变，回滚仅需还原头文件与引用点，无运行时状态迁移。

## Open Questions

- 暂无。`EXEC_FAILURE_STATUS=-126` 等进程退出码已确定不收敛（见 Non-Goals）。
