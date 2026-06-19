## 1. RAM 块后端基础

- [x] 1.1 梳理现有 `BlockDevice`、ATA PIO 后端、设备框架和块 I/O 请求层接口，确认 RAM 后端需要复用的状态、扇区大小和上下文边界
- [x] 1.2 新增 RAM 块设备后端的头文件和实现，提供固定容量、固定扇区大小、清零初始化、整扇区读写和确定性错误状态
- [x] 1.3 覆盖读写参数校验，包括零扇区、缓冲区过小、LBA 范围越界和算术溢出，确保失败写入不改变后端内容
- [x] 1.4 审查后端内存来源和初始化顺序，确认不依赖 hosted runtime、异常、RTTI、无界分配或不可控全局构造

## 2. 设备框架接入

- [x] 2.1 为 RAM 块后端增加内核内部稳定角色和驱动标识，保持用户不可见且不复用 boot disk 或 persistent writable disk 角色
- [x] 2.2 将 RAM 块设备描述符和驱动描述符接入设备 registry/probe 流程，保持重复注册、容量耗尽和 probe 失败的确定性状态
- [x] 2.3 确认同一块设备类别下 ATA 后端与 RAM 后端可按角色独立查找，失败或未 ready 状态不会污染其他角色
- [x] 2.4 保持普通启动路径、MBR/exFAT 发现、persistent `/rw` clean-sync、用户态 ABI 和默认挂载行为不变

## 3. 请求层与 cache 对接

- [x] 3.1 扩展块 I/O 请求层验证，使其通过设备框架查找发布后的 RAM 块后端并提交同步读写请求
- [x] 3.2 覆盖 RAM 后端请求校验失败、未发布设备、设备未 ready、队列满和后端错误的状态传播
- [x] 3.3 验证按设备固定队列隔离，确认 RAM 后端队列压力不影响 ATA 后端，ATA 后端队列压力也不影响 RAM 后端
- [x] 3.4 增加 page/buffer cache 针对 RAM 后端的往返验证，覆盖装入、dirty 修改、同步写回、再次读取和失败时 dirty 状态保留

## 4. 上下文与系统边界

- [x] 4.1 审查 RAM 后端 probe、请求提交和 cache 验证只在普通可阻塞内核上下文运行，禁止从 IRQ、timer、scheduler critical section 或 preemption-disabled 路径触发阻塞 I/O
- [x] 4.2 审查新增设备角色、静态表容量和初始化顺序，确保不改变 boot handoff、链接地址、页表布局、IDT/syscall vector 或磁盘布局
- [x] 4.3 确认文档、注释和诊断只把 RAM 后端描述为有界内核内部验证后端，不声明完整 ramdisk、用户可见设备节点、async I/O、SMP I/O 或新硬件存储支持

## 5. 验证与记录

- [x] 5.1 运行最窄可用的 `xmake` 构建；如缺少 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake 或相关 binutils，记录缺失工具和残余风险
- [x] 5.2 对新增或修改的 C++ 源/头运行 clang/clangd 辅助检查，尽量使用 freestanding C++17、x86_64 target、无异常、无 RTTI 和项目 include 路径；区分历史诊断、当前变更诊断和 freestanding 配置差异
- [x] 5.3 在 QEMU 或 Bochs 可用时运行 RAM 块后端相关默认关闭 smoke，覆盖 framework publication、请求层读写、非法请求、队列隔离和 cache 往返；如模拟器、ROM/显示或磁盘镜像不可用，记录跳过原因
- [x] 5.4 运行或更新相关源级测试，覆盖 RAM 后端边界、设备框架第二后端注册/probe、请求层状态传播和 cache dirty 失败语义
- [x] 5.5 整理验证记录，明确哪些检查通过、哪些检查因工具链或环境不可用而跳过、剩余风险是什么，以及当前变更是否引入新的 clang/clangd 或构建诊断

## 验证记录

- `xmake`：通过，默认关闭 `block_io_request_smoke` 后重新构建通过，确认普通构建路径不依赖 RAM 块验证开关。
- `xmake f --block_io_request_smoke=y && xmake`：通过，确认默认关闭 smoke 打开后可构建。
- `uv run pytest tests/test_device_driver_framework_source.py tests/test_block_io_request_layer_source.py tests/test_writable_fs_page_cache_pipe_source.py tests/test_metadata_consistency_source.py::test_bcache_supports_selected_metadata_writeback_and_dirty_failure_retention`：22 passed。
- `clang++ -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -fsyntax-only -Iinclude -Icpp/include -Icpp/libsupc++/include include/drivers/block/ram_block_device.h kernel/drivers/block/ram_block_device.cc include/bigos/device.h kernel/core/device.cc include/bigos/block_io.h kernel/core/block_io.cc`：通过；仅有 clang 将 `.h` 作为 C++ header 处理的工具警告，无本变更新增语法错误。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/ram-block-serial.log --expect-serial-marker BIGOS_BLOCK_IO_REQUEST_PASSED`：通过，QEMU 串口观测到 `BIGOS_BLOCK_IO_REQUEST_PASSED`；boot artifact 汇编阶段仍有既有 `movsd`/`movsl` 警告。
- 额外说明：一次包含 `tests/test_metadata_consistency_source.py` 全文件的源级测试因历史 OpenSpec 目录 `openspec/changes/add-metadata-consistency/proposal.md` 不存在而失败；已改跑与本变更相关的 bcache dirty 语义用例并通过。未运行 Bochs，因 QEMU 已覆盖本变更默认关闭 smoke；剩余风险为未做 Bochs 交叉验证。
