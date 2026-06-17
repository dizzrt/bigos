# 实现任务 / Implementation Tasks

遵循三段式任务结构（开发 / 输出 / 回归），按子系统拆分且各自可独立评审。

## 1. VMA 模型扩展（开发）

- [x] 1.1 在 `include/bigos/proc.h` 的 VMA 定义中新增 file-backed 只读 backing 类型，携带支撑文件引用与起始文件偏移字段，保持有界 VMA 集合不变量。
- [x] 1.2 在 `kernel/core/proc/proc.cc` 中扩展 VMA 范围校验与用户范围拷贝校验，识别 file-backed 只读区间并拒绝对其写访问。
- [x] 1.3 扩展 teardown/exec 替换路径，正确释放 file-backed VMA 元数据与进程私有 PTE，且不误删仍被引用的共享只读缓存状态。

## 2. 统一缺页入口与缓存物化（开发）

- [x] 2.1 在 `kernel/core/proc`（统一用户缺页入口）新增 file-backed 只读 not-present 读缺页分支：按 `文件偏移 = VMA 文件起始偏移 + (faulting_page - VMA 起始)` 经 page/buffer cache 读入，建立只读非可执行 PTE，推进物化记账。
- [x] 2.2 实现文件尾页零填充：in-file 部分填文件内容，超出文件长度但在 VMA 范围内的部分零填充。
- [x] 2.3 保留 kill 语义：file-backed 只读页的写访问、越界访问、不可阻塞上下文装入分别走确定性权限违例/越界/诊断路径，不进入 COW，不发布部分映射。
- [x] 2.4 在 `kernel/core/fs` 暴露/复用以页为单位读取文件块的 page/buffer cache 接口，支持跨多个缓存块的单页聚合读取，任一块 IO 失败该次物化确定性失败。

## 3. 映射请求 ABI（开发）

- [x] 3.1 在 `include/bigos/syscall.h` 既有 `SyscallNumber` 枚举末尾**追加**专用 syscall（如 `SYS_MAP_FILE`），并在 `kernel/core/syscall/syscall.cc` dispatcher 新增对应 case，参数 `rdi=fd, rsi=offset, rdx=len, r10=permissions, r8=flags`，返回映射用户地址或负 errno；不移动既有 syscall 向量与编号，不碰 `int 0x80` 向量。
- [x] 3.2 校验 fd 为可读常规文件、offset/length 页对齐且不溢出、目标落在受支持用户低半区且不与既有 VMA 重叠、权限不含写与 W+X；失败确定性返回负错误码且不发布部分 VMA。

## 4. fork 共享（开发）

- [x] 4.1 在 `kernel/core/proc/proc.cc` 的 `fork` 地址空间复制路径复制 file-backed VMA 元数据（文件引用、偏移、范围、权限、物化记账），已物化只读页父子共享、未物化部分仅复制元数据，不深拷贝、不进入 COW。

## 5. 验证 smoke（输出）

- [x] 5.1 在 `xmake.lua` 与 `xmake/options.lua` 新增默认关闭的 file-backed 只读映射验证开关，发射固定 COM1/VGA pass/fail marker，且不改动既有默认启动 marker 与 smoke 矩阵。
- [x] 5.2 新增 smoke 用户程序/入口，覆盖：映射创建、首次访问物化命中正确文件内容、文件尾页零填充、越界访问确定性 kill、对只读页写访问确定性 kill。

## 6. C++ 静态检查（回归）

- [x] 6.1 对新增/修改的 C++ 源与头文件运行 clang/clangd 辅助静态检查，配置尽量贴近 GCC 交叉构建（freestanding C++17、x86_64 target、项目 include 路径、无 hosted runtime、无异常、无 RTTI）；修复本次变更引入的错误并确认新告警，区分历史诊断与工具链/freestanding 误报。若 clang/clangd 不可用则记录原因与残余风险。

## 7. 构建与运行时验证（回归）

- [x] 7.1 运行最窄可用的 `xmake` 交叉工具链构建，确认编译通过。
- [x] 7.2 启用 file-backed 映射 smoke 开关，运行 QEMU headless serial-marker 验证（如 `uv run python tools/boot_debug.py run --emulator qemu --display none ...`），记录 pass/fail marker；若 QEMU、Bochs、交叉 binutils、ROM/显示或磁盘镜像不可用，明确记录跳过用例、替代检查与残余 bootability 风险。
- [x] 7.3 确认 boot 固定地址、higher-half base、direct-map 窗口、`KVMEM_BASE`、递归自映射、syscall 向量与 exception/IRQ EOI 语义未被移动或放宽。

## 8. OpenSpec 与文档（回归）

- [x] 8.1 运行 `openspec validate add-file-backed-read-mapping --strict`，确认无解析/结构错误。
- [x] 8.2 同步更新 `docs/en` 与 `docs/zh` 对应文档，描述有界只读 file-backed 映射边界，保持目录结构同构，使用仓库相对路径，不暗示完整 POSIX `mmap`。
