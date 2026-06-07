## 1. 规格与边界确认

- [x] 1.1 对照 `tmp/roadmap.md` 阶段 8、`first-user-program`、`address-space-lifecycle`、`block-device-read`、`exfat-read-filesystem` 主规格，确认 ELF loader 的依赖、非目标和 smoke 边界没有冲突。
- [x] 1.2 确认用户 ELF 文件约定路径、最大文件大小、用户虚拟地址窗口、用户栈大小和 smoke marker 名称，并把决策记录到实现注释或验证记录。
- [x] 1.3 审查 boot fixed addresses、higher-half base、direct map、`KVMEM_BASE`、recursive self-mapping、syscall vector、exception/IRQ gate privilege 和 EOI 语义，确保本 change 不移动或放宽这些 ABI/布局假设。

## 2. 构建与镜像资产

- [x] 2.1 新增默认关闭的 `user_elf_smoke` xmake 配置，确保普通 `xmake` 不编译或运行 ELF 用户程序 smoke 路径。
- [x] 2.2 增加最小 freestanding 用户 ELF 产物构建规则，产物使用静态 ELF64 `ET_EXEC`，通过 `SYS_WRITE` 输出确定性 payload 后调用 `SYS_EXIT`。
- [x] 2.3 将用户 ELF 产物安装到 raw image 的约定 exFAT 路径，并保留现有 flat embedded `user_program_smoke` 的无 FS 依赖回归路径。
- [x] 2.4 若修改 Python 镜像/验证工具，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 验证；若未修改 Python 文件，在验证记录中说明不适用。

## 3. ELF Loader 核心

- [x] 3.1 新增内核态用户 ELF loader 模块和最小公共入口，读取来自 FS 的 kernel buffer，不依赖 hosted libc、异常、RTTI 或宿主文件 IO。
- [x] 3.2 实现 ELF64 header 校验：magic、class、endian、version、machine、type、header size、program header offset/count/entry size 以及所有文件 bounds/overflow 检查。
- [x] 3.3 实现 program header 校验：仅接受 bounded `PT_LOAD`，拒绝 interpreter/dynamic/unsupported header、非法 alignment、越界地址、重叠 segment、入口点不在 executable segment、用户栈碰撞和高半区/内核窗口碰撞。
- [x] 3.4 实现 ELF flag 到用户页属性转换，拒绝 W+X 或权限不兼容的重叠页，确保 text 可执行非 writable、data/bss/stack writable 且 NX。
- [x] 3.5 实现 segment page 分配、page-rounded 映射、file bytes copy 和 bss zero-fill，并确保每个 leaf page 标记为进程 owned 资源。

## 4. FS 到进程运行集成

- [x] 4.1 在 `user_elf_smoke` 路径中通过阶段 7 block/FS API mount/lookup/read 约定用户 ELF 文件，并在文件缺失、过大或读取失败时输出 deterministic `BIGOS_USER_ELF_` failure marker。
- [x] 4.2 创建 ELF-backed `Process`，绑定用户地址空间 root、entry、segment ranges、user stack、kernel stack、exit/fault 状态和 safe teardown 所需 ownership 元数据。
- [x] 4.3 复用既有 TSS/RSP0、`iretq` ring3 entry、`SYS_WRITE`/`SYS_EXIT` 和用户 fault 终止路径，确保 syscall gate 初始化后才进入 CPL3。
- [x] 4.4 保持 flat embedded `user_program_smoke` 与 ELF smoke 独立 selectable 或明确互斥，避免 embedded path 被 block/FS/ELF 依赖污染。

## 5. 失败回滚与生命周期

- [x] 5.1 为 ELF header 校验失败、FS read 失败、临时 buffer 分配失败、segment page 分配失败、page map 失败和 copy/bss 初始化失败定义 bounded error 返回。
- [x] 5.2 在 loader 失败路径释放已分配 kernel buffer、用户 leaf pages、动态用户页表页、用户 PML4 root 和 process kernel stack，且不释放 borrowed high-half kernel mappings。
- [x] 5.3 确认用户 `SYS_EXIT`、CPL3 fault 和非法 user buffer 继续只标记 terminated/faulted/reap-pending，不在当前 syscall/fault 栈上立即释放 active CR3 root 或 active kernel stack。
- [x] 5.4 补充源码级检查覆盖 active-root teardown rejection、current-stack release deferral 和 loader partial-allocation rollback。

## 6. 源码级与辅助静态检查

- [x] 6.1 增加或扩展源码级测试，覆盖 ELF header bounds、program header bounds、用户地址 bounds、segment overlap、W+X rejection、page attr 转换、bss zero-fill 和 load-failure resource release。
- [x] 6.2 增加或扩展源码级测试，确认本 change 未移动 boot/layout 常量、未放宽 exception/IRQ gate DPL、syscall path 不发送 i8259 EOI。
- [x] 6.3 对新增/修改的 C++ 源和头文件运行贴近 freestanding x86_64 C++17 的 clang 辅助检查；若 clang 无法等价配置或不可用，记录 blocker 与剩余风险。
- [x] 6.4 对新增/修改的 C++ 源和头文件运行 clangd 辅助诊断或记录不可用原因，区分历史诊断、freestanding false positive 和本 change 新增问题。

## 7. 构建与运行验证

- [x] 7.1 运行默认 `xmake`，确认普通 boot 构建不依赖用户 ELF 文件、ATA PIO 或 exFAT mount。
- [x] 7.2 运行 `xmake f --user_elf_smoke=y && xmake`，确认 ELF smoke 构建通过，并记录所需 `x86_64-elf-gcc/g++`、xmake 和镜像工具前置条件。
- [x] 7.3 使用 `uv run python tools/boot_debug.py validate-image --image build/test/os.raw` 或等价命令验证 raw image 中存在约定用户 ELF 文件；若工具不支持该检查，记录差距。
- [x] 7.4 尝试 `xmake run bochs-sdl2` 或 `xmake run bochs` 的 ELF user runtime smoke，期望观察 `BIGOS_USER_ELF_` success marker、用户 `SYS_WRITE` payload 或 `BIGOS_USER_EXIT`。
- [x] 7.5 若 Bochs runtime smoke 因 ROM、serial/VGA oracle、image lock 或交互环境不可用而失败，记录不可用依赖、已通过的构建/源码/镜像检查和剩余 bootability 风险。

## 8. OpenSpec 与文档收尾

- [x] 8.1 运行 `openspec validate load-user-elf-program --strict`，修复 proposal/design/spec/tasks 中的格式或语义问题。
- [x] 8.2 如新增用户 ELF 路径、syscall ABI 用法或 smoke 命令，更新 `docs/en` canonical 文档与 `docs/zh` 镜像文档，保持相对路径结构同步。
- [x] 8.3 汇总 validation 记录，明确 passed checks、未运行检查及原因、历史诊断、本 change 新增问题和后续阶段遗留项。
