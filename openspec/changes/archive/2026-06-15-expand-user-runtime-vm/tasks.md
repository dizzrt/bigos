## 1. Runtime Layout Contract

- [x] 1.1 盘点当前用户地址空间、VMA、ELF loader、`brk`、匿名映射、stack、COW、teardown 的实际实现边界，记录与本 change specs 不一致的点。
- [x] 1.2 定义有界用户 runtime VM layout 常量和 helper，覆盖 ELF 段、heap、匿名映射、stack、guard/growth、argument area、reserved future-runtime gaps 和 user low-half bounds。
- [x] 1.3 增加 layout overlap、overflow、kernel-range collision、reserved-gap collision 的校验路径，并确保失败不会发布部分用户映射。
- [x] 1.4 审查 boot 固定地址、higher-half base、direct map、KVMEM、recursive self-mapping、syscall vector、exception/IRQ gate privilege 和 EOI 语义，确认本 change 不移动或扩大这些边界。

## 2. Loader And Image Commit

- [x] 2.1 将 ELF loader 输出整理为 runtime image description，包含 segment ranges、permissions、entry point、stack/argument layout、heap seed、VMA purposes/backing 和 ownership。
- [x] 2.2 将 image preparation 与 process-visible commit 分离，确保 commit 前失败保留旧 image，commit 后资源释放走 lifecycle/reaper 边界。
- [x] 2.3 为 `argv`/`envp` 初始栈布局补齐 bounded size、alignment、address arithmetic 和 rollback 校验。
- [x] 2.4 保持 `PT_INTERP`、dynamic relocation、shared object、unsupported `ET_DYN` 和 runtime dynamic loader 为确定性拒绝，同时预留 inert metadata 扩展点。

## 3. VMA And Page Table Integration

- [x] 3.1 让 VMA insertion 校验 runtime layout region purpose、permissions、backing、growth policy 和 reserved-gap 冲突。
- [x] 3.2 让用户页表 map/unmap 路径同时校验 VMA 与 runtime layout，禁止未授权 user PTE 或权限放宽。
- [x] 3.3 统一 heap、restricted anonymous mapping、stack growth、ELF zero-fill 的 lazy materialization accounting。
- [x] 3.4 更新 fork/COW、shrink/unmap、exec replacement、exit/reap 路径，确保只释放 process-owned low-half state，不释放 borrowed kernel high-half page tables。

## 4. Fault And User-Copy Behavior

- [x] 4.1 更新统一 CPL3 page-fault handler，使其先校验 runtime layout，再按 VMA purpose/backing/growth/permissions 决定 materialization、COW 或 kill。
- [x] 4.2 确保 runtime-reserved gap、stack guard、kernel range、unsupported file-backed range、out-of-layout address 和 invalid COW candidate 都走确定性 user fault path。
- [x] 4.3 更新 user range validation/safe-copy 路径，使其同时校验 runtime layout、VMA 权限和页表可访问性。
- [x] 4.4 保持 CPL0 page fault 的诊断 panic/halt 行为不进入用户 demand-paging 恢复。

## 5. Validation

- [x] 5.1 增加或更新 source-level checks，覆盖 layout non-overlap、reserved-gap rejection、image commit rollback、VMA/layout permission matching、COW ownership 和 teardown ownership。
- [x] 5.2 运行窄范围 `xmake` cross-toolchain build；若 `x86_64-elf-gcc`、`x86_64-elf-g++` 或 xmake 不可用，记录 blocker、替代检查和剩余风险。
- [x] 5.3 对涉及的 C++ 源码和头文件执行辅助 clang/clangd 诊断，使用尽量接近 freestanding C++17/x86_64/no exceptions/no RTTI 的配置；若工具或 flags 不可用，记录差距和剩余风险。
- [x] 5.4 可用时运行 QEMU headless 或 Bochs smoke，覆盖 normal init/userland、exec、demand paging、fork/COW 或 userland runtime 行为；若 emulator、ROM/display、serial oracle 或 disk image 生成不可用，记录跳过原因。
- [x] 5.5 运行 `openspec validate expand-user-runtime-vm --strict` 并修复当前 change 引入的 OpenSpec 结构或规范错误。

## 6. Documentation And Handoff

- [x] 6.1 更新相关开发文档或验证记录，说明 runtime VM layout、image commit、dynamic-linking non-goals 和已执行/跳过的验证。
- [x] 6.2 若修改 `docs/en` 或 `docs/zh`，同步对应语言镜像并保持相同相对路径结构。
- [x] 6.3 在实现完成后检查 `roadmap.md` 是否仍保持项目规划层级；如需更新，只记录 Stage 43 完成状态与高层能力，不加入源代码入口、命令、marker 或版本索引。
