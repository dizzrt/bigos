## 1. 设备框架基础

- [x] 1.1 开发：新增设备框架公共头文件和实现，定义设备类别、稳定角色/id、注册状态、probe 状态、错误状态、class-specific interface 指针和有界静态 registry。
- [x] 1.2 开发：实现设备注册、驱动注册、重复注册检测、容量耗尽处理、按类别/角色查找、probe 单设备和 probe 全部设备的 API，保持 freestanding-safe、无异常、无 RTTI、无 hosted runtime 依赖。
- [x] 1.3 开发：为 registry 操作补充初始化顺序和上下文边界检查，确保可能阻塞或轮询硬件的 probe 不从 IRQ、scheduler critical section、preemption-disabled region 或其他不可阻塞路径执行。
- [x] 1.4 输出：在头文件或源码局部说明中记录该框架是 bounded kernel-internal 注册/probe 边界，不声明热插拔、PCI/ACPI 枚举、完整 bus model、async I/O、SMP 或用户态设备节点。

## 2. ATA PIO 和块设备接入

- [x] 2.1 开发：将现有 ATA PIO boot disk 和 persistent writable test disk 描述为框架可注册的 block 类设备，使用内核内部稳定角色区分二者，probe 成功后发布现有 `BlockDevice` interface。
- [x] 2.2 开发：保持 `BlockDevice` 的整扇区读写校验、512-byte sector 默认契约、read-only rejection、polling timeout/error、flush 失败上报和非 IRQ 上下文边界不变。
- [x] 2.3 开发：为 probe 失败、硬件状态不支持、角色不存在和未发布设备查找失败提供确定性状态，避免向消费者返回半初始化 `BlockDevice`，且不把 boot/persistent disk 角色暴露为用户可见 ABI 或外部验证工具 ABI。
- [x] 2.4 回归：补充源码级检查覆盖 ATA PIO 通过设备框架发布、probe 失败不发布、重复/容量错误不覆盖旧 entry、`BlockDevice` 读写契约未被重排或弱化、内部稳定角色未进入 syscall/用户态头文件。

## 3. PIT/VGA/RTC 正常初始化路径

- [x] 3.1 开发：将 PIT timer 描述为框架可注册的 timer 类设备，probe/publish 后由正常 timer 初始化路径使用，同时保持 PIT channel、IRQ vector、tick 行为和 IRQ handler 边界不变。
- [x] 3.2 开发：将 VGA text device 描述为框架可注册的 video 类设备，probe/publish 后由正常 console/text output 路径使用，同时保持 VGA port/MMIO 假设和可见输出行为不变。
- [x] 3.3 开发：将 CMOS RTC 描述为框架可注册的 rtc 类设备，probe/publish 后由 wall-clock 初始化路径使用，同时保持 bounded one-shot CMOS read 语义不变。
- [x] 3.4 回归：补充源码级检查覆盖 PIT/VGA/RTC 均通过框架注册/probe/publish 接入正常初始化路径，且未重排 interrupt vector、PIT channel、VGA 硬件常量或 RTC 解码边界。

## 4. VFS/bigfs consumer 迁移

- [x] 4.1 开发：调整 VFS boot filesystem 初始化，使其通过设备框架查找 boot disk block interface，不再把直接 ATA PIO 初始化作为正常路径。
- [x] 4.2 开发：调整 bigfs persistent writable 初始化/format 路径，使其通过设备框架查找 persistent writable block interface；设备不可用时保留现有 RAM-backed runtime fallback 边界，且不报告 persistent clean-sync success。
- [x] 4.3 开发：确认 page/buffer cache、exFAT mount、persistent `/rw` clean-sync 和 metadata commit 路径继续只依赖 `BlockDevice` contract，不依赖具体 ATA 初始化 helper。
- [x] 4.4 回归：补充源码级检查覆盖 VFS/bigfs 不绕过框架直接构造硬件-backed block device、boot disk 查找失败返回确定性错误、persistent fallback 不扩大持久化承诺。

## 5. 初始化顺序和低层边界复核

- [x] 5.1 开发：把设备框架初始化和 probe 放入合适的内核启动顺序，位于基础端口 I/O/内存设施可用之后、VFS/bigfs 依赖块设备之前，并覆盖 PIT/VGA/RTC 的正常初始化入口。
- [x] 5.2 复核：确认本 change 不改变启动地址、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI、现有 syscall 编号或默认用户态行为。
- [x] 5.3 复核：检查 IRQ、PIC、keyboard、syscall 和 scheduler 路径不会隐式触发阻塞 probe 或设备注册；确认 PIT handler 只消费已发布 timer state，不在 IRQ path 发起 probe。
- [x] 5.4 输出：整理 validation notes，区分已通过检查、无法运行的检查及原因、历史诊断、当前变更新增并已修复的问题和剩余风险。

## 6. 运行时验证

- [x] 6.1 回归：在工具链和 QEMU 可用时运行现有 boot/filesystem 相关 headless smoke，确认 boot asset 读取仍能到达预期成功路径；若 QEMU、ROM/display、serial capture 或磁盘镜像配置不可用，记录 skipped/blocked 与残余风险。
- [x] 6.2 回归：在 persistent writable 配置可用时运行 `/rw` 相关写入、同步和 clean reboot readback smoke，确认 persistent block device 经框架发布后行为不回退；不可用时记录手动 setup 缺口。
- [x] 6.3 回归：运行 timer/console/RTC 相关窄验证或 smoke，确认 PIT tick、VGA/serial 可见输出和 CMOS RTC wall-clock read 在框架接入后不回退；不可用时记录 skipped/blocked 与残余风险。
- [x] 6.4 回归：如需要 Bochs 验证早期 ATA/port I/O/PIT 行为，运行 Bochs 或记录本地 Bochs ROM/display/磁盘路径缺失原因；QEMU passed 不能替代未运行的 Bochs 结论。
- [x] 6.5 输出：若新增默认关闭 smoke marker 或验证说明，保持 marker/validation 记录与 bounded 设备框架范围一致，不声称 async I/O、第二后端、完整设备模型、用户可见设备 ABI 或 SMP。

## 7. 静态检查与构建

- [x] 7.1 回归：运行相关源码级 pytest，例如 `uv run pytest tests/test_writable_fs_page_cache_pipe_source.py tests/test_fd_vfs_shell_source.py tests/test_runtime_filesystem_maturity_source.py tests/test_timer_irq_foundation_source.py tests/test_time_and_identity_source.py`，并补充/修复设备框架相关源码检查；若 `uv` 不可用，记录 blocker。
- [x] 7.2 回归：若新增或修改 Python 测试/脚本，运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`，并修复本 change 引入的 lint/type/test/format 问题。
- [x] 7.3 回归：运行 xmake 交叉构建或等价窄构建，确认 C++/C/assembly 变更可由 `x86_64-elf-*` toolchain 编译；缺少 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake 或 binutils 时记录缺失项和残余风险。
- [x] 7.4 回归：对新增/修改的 C++ 源和头文件执行 clang/clangd 辅助静态检查，尽量使用 freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI 配置；区分历史诊断、当前变更诊断和 freestanding 配置误报。

## 8. 收尾

- [x] 8.1 输出：运行 `openspec status --change add-device-driver-framework`，确认 proposal、design、specs、tasks 均完成且 apply-ready。
- [x] 8.2 输出：复核 OpenSpec artifact 未引用路线图任务编号，且 capability 名称、非目标、架构/内存/仿真器/磁盘/工具链假设保持一致。
