## ADDED Requirements

### Requirement: 多队列注册等待支持

BigOS SHALL 扩展调度阻塞原语，使一个非中断内核线程能在同一次阻塞中同时登记到多个 wait queue 上，并在其中任一 wait queue 被唤醒或可选超时到期时被唤醒恰好一次。该多队列等待 MUST 复用既有 monotonic tick 超时机制，MUST 保持登记、注销与唤醒在分配无关、IRQ-safe 的约束内完成，MUST NOT 改变既有单队列 `wait_queue_wait_until`/`wake_one`/`wake_all` 的对外语义，也 MUST NOT 引入抢占式调度、SMP 迁移或用户可见 POSIX 语义。等待线程的多队列登记节点 MUST 来自线程自身的稳定存储，唤醒路径 MUST NOT 依赖普通动态分配。

#### Scenario: 线程同时等待多个 wait queue

- **WHEN** 一个允许阻塞的非中断线程在集合内所有目标条件均不满足时，通过多队列等待原语同时登记到多个 wait queue
- **THEN** BigOS MUST 在使线程非可运行之前原子地记录其对这些 wait queue 的成员关系
- **AND** 调度器 MUST 切换到另一可运行线程或 idle 线程而不破坏 run queue

#### Scenario: 任一队列唤醒使多队列等待线程可运行

- **WHEN** 一个唤醒作用于多队列等待线程所登记的其中一个 wait queue
- **THEN** BigOS MUST 使该线程恰好一次地重新可运行
- **AND** 该线程恢复后 MUST 从其登记的所有 wait queue 注销其等待节点
- **AND** 唤醒 MUST NOT 要求普通动态分配

#### Scenario: 多队列等待超时到期

- **WHEN** 一个多队列等待线程带有限超时且在被任一队列唤醒前 monotonic tick 到达其 deadline
- **THEN** BigOS MUST 以确定性超时结果使该线程重新可运行
- **AND** 超时转换 MUST NOT 在 IRQ 上下文要求普通动态分配

#### Scenario: 多队列等待不改变单队列语义

- **WHEN** 多队列等待支持被引入
- **THEN** 既有单队列 `wait_queue_wait_until`、`wake_one`、`wake_all` 的入队、唤醒幂等与空队列安全行为 MUST 保持不变
- **AND** 对既有单等待线程的唤醒 MUST NOT 被多队列登记节点干扰
