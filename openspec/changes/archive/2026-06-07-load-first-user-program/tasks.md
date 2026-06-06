## 1. 用户程序镜像与构建接入

- [x] 1.1 确定首个用户程序格式（ELF64 或 flat blob），记录选择理由和不依赖内核 FS/块设备的边界。
- [x] 1.2 新增最小 freestanding 用户程序源码或二进制生成输入，使其只执行 bounded write + exit 闭环。
- [x] 1.3 更新 `xmake.lua` 或等价构建脚本，把用户程序作为内嵌镜像、链接对象或符号区间提供给内核。
- [x] 1.4 新增默认关闭的 `user_program_smoke` 构建开关，默认 boot 不运行用户程序路径。

## 2. 最小进程与地址空间运行路径

- [x] 2.1 新增 `proc` 或等价最小进程结构，记录 PID/身份、用户地址空间根、用户入口、用户栈、状态和 exit code。
- [x] 2.2 实现用户程序加载器，映射代码、只读数据、数据/BSS 和用户栈，并使用显式 user/NX/writable 页属性。
- [x] 2.3 实现受控地址空间激活 API，只在进程运行路径切换到派生用户根，并保持内核高半区/direct map/KVMEM/self-mapping 可达。
- [x] 2.4 处理加载或映射失败路径，输出确定性 `BIGOS_USER_` marker 或走统一 panic，禁止进入部分初始化的 ring3。

## 3. x86_64 ring3 进入基础

- [x] 3.1 补齐 GDT 用户 code/data selector 与 TSS/RSP0 或等价内核栈返回机制，保持 boot 固定地址和既有 selector ABI 不被静默移动。
- [x] 3.2 将 `VECTOR_SYSCALL = 0x80` gate 显式配置为允许 CPL3 软件触发，并确认其它 exception/IRQ gate 不被放宽。
- [x] 3.3 实现 `iretq` ring3 进入 helper 或等价路径，构造 user RIP/CS/RFLAGS/RSP/SS frame。
- [x] 3.4 在 `user_program_smoke` 路径中创建首个用户进程并进入用户入口，默认关闭时保持现有 scheduler/idle 行为。

## 4. 用户态 syscall 闭环

- [x] 4.1 扩展 syscall number 定义，新增或收敛最小 `write` 与 `exit` 语义，同时保持阶段 5 ABI 的寄存器约定。
- [x] 4.2 实现用户 buffer range 检查或 safe copy helper，验证 user 地址、present/user bit 和 bounded length。
- [x] 4.3 实现 `write` syscall，经 console/serial 诊断路径输出确定性 `BIGOS_USER_` marker 并返回确定性结果。
- [x] 4.4 实现 `exit` syscall，记录 exit code、标记当前进程 terminated，并确保不返回已终止用户流。
- [x] 4.5 确认 syscall path 不发送 i8259 EOI，不从 syscall handler 调用 non-IRQ-safe allocator 或执行非 bounded 输出。

## 5. 用户态 fault 与生命周期边界

- [x] 5.1 扩展 `#PF` 诊断路径，使用 saved CS/CPL 或等价信息区分用户 fault 与内核 fault。
- [x] 5.2 对用户态页错误、非法 syscall 指针和不可恢复用户异常输出确定性 marker 或终止进程，不实现 demand paging。
- [x] 5.3 保持内核态 `#PF` 的诊断-only 语义，不把内核 fault 误判为用户 fault。
- [x] 5.4 记录 terminated 进程和相关栈/地址空间对象的延后回收限制，避免在当前栈上立即释放。

## 6. 文档与源码级检查

- [x] 6.1 更新 `docs/en/arch` 中用户态进入、syscall ABI、用户地址空间激活、用户指针检查和 fault 处理说明。
- [x] 6.2 新增源码级测试覆盖用户镜像 wiring、页属性、CR3 激活边界、ring3 frame、syscall gate DPL、用户指针校验和 exit 行为。
- [x] 6.3 新增源码级测试确认 boot 固定地址、higher-half base、direct map、`KVMEM_BASE`、self-mapping 地址和 `InterruptFrame` ABI 未被移动。
- [x] 6.4 新增源码级测试确认 exception/IRQ/syscall EOI 语义分离，只有 syscall vector 可被 CPL3 软件触发。

## 7. 验证与记录

- [x] 7.1 运行 `uv run pytest` 的相关源码级测试集合，并记录通过项、历史失败项和当前 change 引入的问题。
- [x] 7.2 运行默认 `xmake`，确认未启用 smoke 时默认 boot 构建行为不变。
- [x] 7.3 运行 `xmake f --user_program_smoke=y` 和对应构建，确认用户程序 smoke 被编入且默认关闭配置可恢复。
- [x] 7.4 对新增/修改 C++ 和汇编路径运行最窄可用 freestanding syntax/clang/clangd 辅助检查；若工具链不可用，记录 blocker 与剩余风险。
- [x] 7.5 运行 `openspec validate load-first-user-program --strict`。
- [x] 7.6 在 Bochs/serial/VGA oracle 可用时运行用户程序 marker smoke；若不可用，记录命令、失败点、环境缺失和剩余 bootability 风险。
- [x] 7.7 新增或更新 validation 记录，分离已通过检查、无法运行检查、历史诊断和当前 change 引入的问题。
