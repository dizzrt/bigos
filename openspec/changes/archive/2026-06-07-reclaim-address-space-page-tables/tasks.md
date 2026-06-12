## 1. 页表 ownership 与回收模型

- [x] 1.1 梳理当前 `kernel/mm` 页表 map/unmap、`free_pages()`、user root-targeted mapping 和 direct-map 访问路径，记录哪些页表页是 static/borrowed，哪些可以成为动态 owned 页表页。
- [x] 1.2 设计并实现动态页表页 metadata，至少表达 owner/category、level、physical frame、present-entry count 或等价可验证不变量。
- [x] 1.3 将 metadata 创建接入中间页表页分配路径，确保 publish present descriptor 前已完成 ownership 登记。
- [x] 1.4 为 metadata 分配失败、页表页分配失败和 descriptor 写入失败补齐 rollback 或 deterministic fatal path，禁止留下未追踪的 reclaimable present descriptor。
- [x] 1.5 增加源码级检查，覆盖 static/borrowed 页表页不会被标记为 reclaimable，dynamic 页表页 owner/level/count 初始化正确。

## 2. map/unmap 与空页表回收

- [x] 2.1 扩展 leaf PTE map/unmap 边界，使 present-entry accounting 在新增映射、覆盖失败、清除映射和 rollback 中保持一致。
- [x] 2.2 实现从 leaf unmap 向上检查空 PT/PD/PDPT 的 helper，只释放 owner 匹配且 present-entry count 为 0 的动态页表页。
- [x] 2.3 确认 non-owned、borrowed、高半区 shared、direct map、KVMEM、recursive self-mapping 和 boot/kernel 页表页不会被空页表回收路径释放。
- [x] 2.4 对每个被清除的 leaf PTE 或非叶 descriptor 执行当前单核 TLB invalidation，或在 inactive-root teardown 中记录不需要立即 `invlpg` 的前提。
- [x] 2.5 更新 `free_pages()` 或其页表 unmap 下游路径，使 kernel virtual range 释放后可安全回收动态空页表页，同时保持公开 API 的页数语义不变。

## 3. 用户地址空间 teardown

- [x] 3.1 为用户地址空间新增 teardown helper，只遍历用户低半区 process-owned leaf mappings 和动态页表页。
- [x] 3.2 释放用户 code/data/BSS/stack leaf physical pages，并确保不会释放 borrowed/shared kernel physical pages。
- [x] 3.3 在所有 owned low-half 子页表释放后释放用户 PML4 root，并拒绝或延后释放 active CR3 root。
- [x] 3.4 为 teardown 失败路径提供 deterministic marker、错误返回或统一 panic，避免继续执行不可推理的部分回收状态。
- [x] 3.5 增加源码级检查，覆盖用户低半区遍历、高半区 borrowed entries 保留、PML4 root 最后释放和 active-root 拒绝。

## 4. 进程退出与 fault 回收边界

- [x] 4.1 扩展 `Process` 或等价结构，记录 process-owned 资源、terminated/faulted/reap-pending 状态、exit code、fault reason 和 kernel stack range。
- [x] 4.2 调整 `SYS_EXIT` 路径，使其只记录 exit code、标记 terminated/reap-pending，并禁止返回用户指令流或在当前 syscall 栈上释放资源。
- [x] 4.3 调整用户态 `#PF` 和非法用户 buffer 处理，使其记录 fault reason、标记 faulted/reap-pending，并保持 kernel-mode `#PF` diagnostic-only 语义。
- [x] 4.4 实现安全 reaper handoff，可在非 IRQ、safe kernel root、非目标 kernel stack 的上下文中调用 process teardown。
- [x] 4.5 为 process kernel stack 释放增加当前栈范围检查；仍在 active stack 上时必须延后或 fail safely。

## 5. 初始化顺序与低层安全审查

- [x] 5.1 审查 memory initialization order，确认页表 metadata 所需分配路径不会破坏 buddy early metadata arena、slab/kmalloc 初始化和 VMem 启动顺序。
- [x] 5.2 审查 allocation phase、object lifetime、alignment 和 failure behavior，确认页表页 frame、metadata 对象、用户 leaf page、PML4 root 和 kernel stack 都有唯一释放责任。
- [x] 5.3 审查 bootability 和地址布局，确认 boot fixed addresses、higher-half base、kernel load base、`KVMEM_BASE`、direct-map window、recursive self-mapping 和 BootInfo ABI 均未移动。
- [x] 5.4 审查 interrupt/syscall 边界，确认 teardown 不在 IRQ handler 中运行，syscall path 不发送 i8259 EOI，exception/IRQ gate DPL 不被放宽。
- [x] 5.5 更新相关架构文档或验证记录，说明用户地址空间 teardown、页表 ownership、safe reaper 和非目标边界；若修改 `docs/en`，同步对应 `docs/zh`。

## 6. 源码级测试与静态检查

- [x] 6.1 新增或更新 `uv run pytest` 源码级检查，覆盖页表 ownership metadata、present-entry accounting、metadata failure rollback 和 empty PT/PD/PDPT reclaim。
- [x] 6.2 新增或更新源码级检查，覆盖 user teardown 只释放 owned low-half、保留 borrowed high-half、拒绝 active root、延后 active stack release。
- [x] 6.3 新增或更新源码级检查，确认 `SYS_EXIT`、用户 `#PF`、非法用户 buffer 均只标记待回收并通过 safe teardown 路径释放资源。
- [x] 6.4 新增或更新源码级检查，确认 boot/address constants、`InterruptFrame` ABI、syscall vector、exception/IRQ/syscall EOI 语义没有被当前 change 移动或放宽。
- [x] 6.5 对新增/修改 C++ 源码和头文件运行接近 cross-build 的 clang 辅助检查；若 clang 无法表达 freestanding x86_64/no-exceptions/no-RTTI 配置，记录差距和剩余风险。
- [x] 6.6 对新增/修改 C++ 源码和头文件运行 clangd 辅助诊断；区分历史诊断、当前 change 新增诊断和 freestanding 配置 false positive，并修复当前 change 引入的有效问题。

## 7. 构建与 runtime 验证

- [x] 7.1 确认本地具备 `x86_64-elf-gcc`、`x86_64-elf-g++`、xmake、Bochs 和需要的 ROM/disk image 配置；缺失时在验证记录中明确 blocker 和剩余风险。
- [x] 7.2 运行默认 `xmake`，确认未启用 smoke 时默认 boot 构建不受 process teardown 和页表回收 wiring 影响。
- [x] 7.3 运行 `xmake f --user_program_smoke=y` 后的最窄构建，确认 `kernel/core/proc/**`、syscall exit/fault 和 address-space teardown 代码可编译。
- [x] 7.4 在 Bochs/serial oracle 可用时运行用户程序 smoke，观察 `BIGOS_USER_EXIT` 或新增 deterministic reclaim marker；不可用时记录命令、失败点、环境缺失和 bootability 风险。
- [x] 7.5 如修改 memory self-test 或新增内存 smoke，运行对应 `xmake f --mm_self_test=y` 与 `uv run python tools/boot_debug.py ... --expect-serial-marker ...`；不可用时记录原因。
- [x] 7.6 运行 `openspec validate reclaim-address-space-page-tables --strict`，修复 proposal/design/spec/tasks 中的格式或 requirement 问题。
- [x] 7.7 新增或更新 validation 记录，分离已通过检查、无法运行检查、历史诊断、当前 change 引入的问题和环境/toolchain false positive。
