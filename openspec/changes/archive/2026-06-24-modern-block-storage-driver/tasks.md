## 1. 前置依赖与设计确认

- [x] 1.1 确认 `pci-config-access-and-vector-alloc` 与 `pci-msix-interrupt-delivery` 已实现并可用（PCI 访问/capability/BAR、向量分配、MSI-X 投递）
- [x] 1.2 梳理设备框架注册/probe/publish、块请求层 issue/completion、内核虚拟内存映射与物理页分配接口
- [x] 1.3 确认首版边界：modern-only transport、MSI-X 完成、单 split virtqueue、内核内部稳定角色、不改默认启动

## 2. virtio modern PCI transport

- [x] 2.1 解析 virtio vendor-specific capability，定位并映射 common/notify/ISR/device cfg 区域（有界不可缓存 MMIO）
- [x] 2.2 实现 device reset、ACKNOWLEDGE/DRIVER status 推进、feature 协商（含 VIRTIO_F_VERSION_1）、FEATURES_OK 校验、DRIVER_OK
- [x] 2.3 实现失败路径：协商/初始化失败置 FAILED status 并确定性诊断；仅 legacy 设备判不支持不发布

## 3. split virtqueue 与请求提交

- [x] 3.1 分配对齐内核物理页，初始化 descriptor table/available ring/used ring，写入 common cfg queue 字段，容量为有界静态值
- [x] 3.2 实现块读请求 descriptor 链（请求头 + 设备可写数据 + 状态字节），写 available ring 并 notify
- [x] 3.3 实现块写请求 descriptor 链（请求头 + 设备可读数据 + 状态字节），写 available ring 并 notify
- [x] 3.4 通过块请求层 issue 边界进入 pending，DMA 缓冲以物理地址提供给设备

## 4. MSI-X 完成接入

- [x] 4.1 通过 MSI-X 能力为 virtqueue 完成中断分配并编程一个向量，注册 IRQ-safe completion 入口
- [x] 4.2 completion 入口解析 used ring，按块请求层 token/generation 完成 pending 请求并唤醒等待者
- [x] 4.3 确认 completion 入口 allocation-free、nonblocking、经 LAPIC EOI、不发 PIC EOI、不做 cache/FS policy；迟到/不匹配完成被拒绝或诊断

## 5. 设备框架发布与错误映射

- [x] 5.1 通过设备框架以内核内部稳定角色注册/probe/publish virtio-blk，不改变默认启动设备
- [x] 5.2 映射 virtio-blk 状态字节错误、传输失败与 timeout 为块请求层确定性失败状态，保持与 ATA 一致生命周期

## 6. 默认关闭验证入口

- [x] 6.1 新增默认关闭构建开关，串联发布、写后读往返校验、设备错误/timeout、MSI-X 完成闭环的 smoke 入口，按既有 COM1/VGA marker 风格输出 pass/fail
- [x] 6.2 添加源级检查：completion 入口 IRQ-safe 与 EOI 唯一、probe 仅在可阻塞上下文、角色不暴露用户 ABI、默认启动不依赖 virtio-blk

## 7. 构建与静态检查

- [x] 7.1 运行 `xmake`（x86_64-elf-gcc/g++）确认默认配置与启用验证开关均可编译
- [x] 7.2 运行 clang 与 clangd 辅助静态检查（freestanding C++17、x86_64 target、项目 include、无 hosted/异常/RTTI），修复本次引入 error 并确认/修复有效新增 warning；工具不可用则记录原因与残余风险
- [x] 7.3 区分历史诊断、本次变更诊断与工具链/freestanding 误报

## 8. 仿真器验证与记录

- [x] 8.1 在 QEMU（`--display none`，配置 modern virtio-blk + MSI-X）下运行启用验证开关的 smoke，观察发布/读写/完成 marker，日志写入 `logs/`
- [x] 8.2 默认启动回归：确认默认关闭验证时 boot/shell/`/rw`/userland baseline 行为不变，且不依赖 virtio-blk
- [x] 8.3 记录验证：通过项、因 QEMU/Bochs/virtio-blk/MSI-X 支持/工具链/磁盘镜像不可用而跳过的项与残余风险分别记录，不对跳过环境声明运行时成功

## 9. 中断/驱动/内存安全审查

- [x] 9.1 审查中断安全与重入：completion 入口不阻塞/不分配，分发与 EOI 所有权未被破坏
- [x] 9.2 审查内存与硬件访问：virtqueue 物理地址与对齐、MMIO 映射不可缓存、DMA 缓冲生命周期、可见失败路径确定性
- [x] 9.3 审查地址/布局假设：未改 boot handoff、链接地址、页表布局、向量分配与磁盘布局
