## ADDED Requirements

### Requirement: 有界多路复用 syscall

BigOS SHALL 在既有有界 syscall surface 上以 append-only 方式新增一个多路复用操作，让简单静态用户程序对一个定容描述符集合带毫秒超时等待并逐项查询可读/可写/错误就绪。该操作 MUST 通过追加新的 syscall 编号（例如 `SYS_POLL`）引入，MUST NOT 改变任何既有 syscall 编号、寄存器参数顺序、`int 0x80` 返回约定、syscall gate 特权或 syscall no-EOI 规则。该操作 MUST 复用统一 fd 就绪模型产生就绪位、复用调度等待队列实现带超时阻塞，并保持有界：MUST NOT 实现完整 POSIX `poll`/`select`/`epoll`/`ppoll`、无界描述符集合、边缘触发、事件通知对象或信号中断的复杂 restart 语义。

#### Scenario: 多路复用 syscall 追加而不改号

- **WHEN** 多路复用 syscall 被引入
- **THEN** BigOS MUST 在既有 syscall surface 之后追加新的 syscall 编号，或使用文档化的未占用条目
- **AND** 所有既有 syscall 编号、寄存器参数顺序、`int 0x80` 返回行为、syscall gate 特权、异常/IRQ EOI 规则、boot 布局、页表布局、CR3 切换与磁盘镜像布局 MUST 保持不变

#### Scenario: 就绪查询报告逐描述符就绪

- **WHEN** 用户程序对一个有效的定容描述符集合调用该 syscall 且至少一个描述符满足其请求的可读或可写条件
- **THEN** BigOS MUST 复用统一就绪模型在对应集合项回填就绪事件位并返回就绪个数
- **AND** 该查询 MUST 是只读的：MUST NOT 出队数据或改变描述符偏移/打开状态

#### Scenario: 无就绪带超时阻塞

- **WHEN** 集合内当前无描述符就绪、调用方允许阻塞且传入正毫秒超时
- **THEN** BigOS MUST 复用调度等待队列让当前线程真正阻塞让出，并在集合内任一描述符就绪或超时到期时唤醒
- **AND** 超时到期且无就绪时 MUST 返回 0，且 MUST NOT 向用户泄漏调度器私有等待常量

#### Scenario: 非法参数确定性失败

- **WHEN** 该 syscall 收到超过定容上限的描述符个数、不可访问的用户数组指针或不支持的参数
- **THEN** BigOS MUST 返回确定性负 errno
- **AND** it MUST NOT 阻塞、部分处理集合或破坏用户内存与 fd 表状态
