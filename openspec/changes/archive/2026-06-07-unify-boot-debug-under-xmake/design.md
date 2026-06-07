## Context

BigOS 当前以 `xmake` 作为主构建系统，但本地启动调试链路仍跨越多个入口：

- 根 `Makefile` 提供 `boot-debug`、`boot-debug-gui` 和 `boot-debug-user-gui` 包装。
- `tools/boot_debug.py` 负责编译 kernel、调用 boot 子目录 Makefile、生成 raw image、生成 Bochs 配置并启动 Bochs。
- `src/arch/x86/boot/Makefile` 负责编译 `mbr.bin`、`dbr.bin`、`exdbr.bin` 和 `boot.bin`。
- `xmake.lua` 已经定义 kernel target 和所有 smoke 开关，但 `xmake run kernel` 只直接启动 Bochs，不走 deterministic raw image 生成流程。

这导致两个实际问题：

- 工具链割裂：开发者需要同时理解 `make`、`xmake` 和 Python helper 的职责。
- 配置割裂：`boot_debug.py` 当前只显式处理 `mm_self_test` 和 `user_program_smoke`，运行时会执行固定 `xmake f ...`，从而清掉 `timer_smoke`、`syscall_smoke`、`user_vmem_smoke` 等已保存配置。

本设计只调整开发期构建和调试编排，不改变 boot protocol、disk layout、ELF 加载、kernel link address、interrupt/syscall ABI、page-table 布局或内核运行时初始化顺序。

目标控制流：

```text
xmake f --user_program_smoke=y --syscall_smoke=y
        │
        ▼
保存到 .xmake/<plat>/<arch>/cache/config
        │
        ▼
xmake run bochs-sdl2
        │
        ├─ 按当前 xmake 配置构建 kernel
        ├─ 构建 MBR / DBR / exDBR / boot.bin
        ├─ 生成 build/test/os.raw
        ├─ 生成 build/test/bochsrc.bxrc，追加 display_library: sdl2
        └─ 启动 bochs -f build/test/bochsrc.bxrc -q
```

## Goals / Non-Goals

**Goals:**

- 用 `xmake` 统一本地 Legacy BIOS boot debug 的构建和运行入口。
- 让 `xmake` 无参默认继续只构建 `kernel`。
- 提供 `xmake run bochs-sdl2` 作为 Bochs SDL2 GUI 调试入口，并同时提供非 SDL2 的 `xmake run bochs` 后备入口。
- 保留后续扩展 `bochs-term`、`qemu`、`qemu-ovmf` 等 backend 的命名空间。
- 将 boot-stage artifact 构建纳入 `xmake.lua`，移除对 boot 子目录 Makefile 的依赖。
- 让 `xmake run bochs-sdl2` 使用当前 `.xmake` 中保存的配置，支持 `user_program_smoke`、`syscall_smoke` 等开关组合。
- 保留 `tools/boot_debug.py` 的 raw image 生成、Bochs config 生成、serial-marker smoke 和 `--no-launch` 离线生成能力，但移除其固定重配置 smoke 开关的行为和 smoke 快捷参数。
- 更新文档、测试和 OpenSpec 要求，使推荐入口与实际工具链一致。

**Non-Goals:**

- 不实现 QEMU、QEMU + OVMF 或 UEFI boot debug backend。
- 不改变 Legacy BIOS/MBR/exFAT image layout、`/boot/boot.bin` 和 root `kernel` 文件名、cluster layout 或 bootloader lookup 规则。
- 不改变 kernel link address `0xffffffff80000000`、linker script、ELF segment 加载语义或 boot handoff ABI。
- 不改变任何 smoke 的内核运行时触发顺序、marker 字符串或 syscall/user-mode ABI。
- 不引入 hosted runtime、外部 Python 依赖或新的系统级工具依赖。

## Decisions

### Decision: 以 xmake run target 作为稳定调试入口

采用 `xmake run bochs-sdl2` 表示“按当前 xmake 配置构建并运行 Bochs SDL2 backend”。该入口不是“只启动已有镜像”，而是完整调试闭环：构建 kernel、构建 boot artifacts、生成 raw image 和 Bochs 配置、启动 emulator。

同时提供 `xmake run bochs` 作为非 SDL2 后备入口，用于默认 Bochs display 或不强制 GUI display 的环境。两个入口共享构建、镜像生成和配置保持语义，差异仅在 generated bochsrc 的 display 配置。

替代方案：

- 继续保留 `make boot-debug-gui`：无法解决工具链割裂，也无法自然复用 xmake 已保存配置。
- 使用 `uv run python tools/boot_debug.py run --bochs-extra ...` 作为主入口：仍会让 Python helper 成为主构建编排入口，且不符合项目“xmake 是 primary build system”的约束。
- 使用单个自定义 task 如 `xmake boot-debug --backend=bochs-sdl2 --syscall_smoke=y`：可做到一条命令配置并运行，但会绕过 xmake 原生 `f/config` 和 `run` 语义，增加额外参数解析复杂度。

### Decision: xmake f 负责持久化 smoke 配置

`xmake f --user_program_smoke=y --syscall_smoke=y` 继续作为配置入口，配置保存在 `.xmake/<plat>/<arch>/cache/config`。`xmake run bochs-sdl2` 必须读取当前配置并构建，不得在运行阶段执行会重置未列出开关的固定 `xmake f` 命令。

替代方案：

- 给 `boot_debug.py` 增加所有 smoke 参数：短期可用，但每新增 smoke 都需要同步 Python CLI，仍存在 xmake 配置和 Python 参数双源问题。
- 让 `xmake run bochs-sdl2` 接受 `--user_program_smoke=y`：不符合 xmake target run 参数的常规职责，也容易与传给 emulator 或 helper 的参数混淆。

### Decision: boot-stage artifacts 纳入 xmake 构建图

将 `mbr.s`、`dbr_exfat.s`、`exdbr_exfat.s`、`boot.s` 和 `boot.cc` 的构建规则迁入 `xmake.lua` 或 xmake include 文件，输出继续保持在 `build/bin/x86/boot/`：

```text
build/bin/x86/boot/mbr.bin
build/bin/x86/boot/dbr.bin
build/bin/x86/boot/exdbr.bin
build/bin/x86/boot/boot.bin
```

大小上限保持不变：

- `mbr.bin` <= 512 bytes
- `dbr.bin` <= 512 bytes
- `exdbr.bin` <= 4096 bytes
- `boot.bin` <= 524288 bytes

替代方案：

- 保留 boot 子目录 Makefile，仅从 xmake 调用 `make -C src/arch/x86/boot ...`：虽然可快速接入 `xmake run`，但没有真正移除 Makefile 工具链。
- 改写为 Python 构建 boot artifacts：会把编译职责从 xmake 转移到 helper，不符合统一主构建系统的目标。

### Decision: boot_debug.py 降级为镜像和 emulator helper

`tools/boot_debug.py` 可继续提供以下能力：

- deterministic raw image 生成和校验
- generated bochsrc 渲染
- Bochs 启动和 serial marker bounded smoke
- `--no-launch` 离线生成校验路径

但它不再负责固定执行 `xmake f --mm_self_test=... --user_program_smoke=...`，也不保留 `--memory-self-test` / `--user-program-smoke` 这类 smoke 快捷参数。开发者必须先通过 `xmake f` 配置需要的 smoke 开关，再运行 `xmake run bochs-sdl2`、`xmake run bochs` 或 Python helper 的离线路径，避免再次形成 xmake 配置和 Python CLI 参数双源。

替代方案：

- 删除 `boot_debug.py`：不合适，raw image 生成和 serial-marker smoke 已经稳定且不需要进入 kernel runtime。
- 将 image 生成完全改写为 xmake Lua：会增加迁移风险，且 Python 标准库实现已经有测试覆盖。

### Decision: backend 命名以 emulator/display 组合表达

首批稳定 target 包含 `bochs-sdl2` 和 `bochs`。`bochs-sdl2` 表示 Bochs backend + SDL2 display，`bochs` 表示 Bochs backend + 默认 display 或不强制 display。未来可扩展：

```text
bochs
bochs-sdl2
bochs-term
qemu
qemu-ovmf
```

该命名避免把 `gui` 作为不明确抽象，也为 UEFI/QEMU 后续规划保留空间。

### Decision: no-launch 只保留在 helper 路径

`bochs-sdl2` 和 `bochs` run target 不支持 `--no-launch` run argument，保持语义简单：run target 就是构建并启动调试 backend。离线 image 生成和 image validation 继续通过 `tools/boot_debug.py` 的 helper 路径提供，例如迁移后的 no-launch 等价命令。

替代方案：

- 在 `xmake run bochs-sdl2 -- --no-launch` 中透传参数：会让 run target 同时承担启动和离线生成两套语义，增加使用和测试复杂度。
- 新增单独 `xmake run bochs-sdl2-no-launch` target：target 组合膨胀较快，且离线生成已经由 Python helper 覆盖。

## Risks / Trade-offs

- [Risk] xmake target 依赖建模不当导致 `xmake run bochs-sdl2` 启动旧 kernel 或旧 boot artifacts。→ Mitigation: 调试 target 必须依赖 `kernel` 和 boot-stage artifact targets，并在 image 生成前显式检查产物存在与大小上限。
- [Risk] 从 Makefile 迁移 boot-stage 构建时改变 `-Ttext`、`--oformat binary`、entry symbol 或 object 顺序。→ Mitigation: 迁移时逐项对照 Makefile 参数，并用 artifact size、file presence、`boot_debug.py validate-image` 和 boot smoke 验证。
- [Risk] `boot_debug.py` 和 xmake 之间形成循环调用。→ Mitigation: xmake 负责构建；Python helper 只消费已构建 artifacts 并生成 image/config，不再调用会重配工程的 xmake config 阶段。
- [Risk] 删除 `make boot-debug` 与当前 OpenSpec/文档历史约束冲突。→ Mitigation: 本 change 明确修改 `one-command-boot-debug` 要求，将稳定入口迁移到 xmake，并同步 docs/en 与 docs/zh。
- [Risk] Bochs SDL2 在部分机器不可用。→ Mitigation: `bochs-sdl2` 缺少 Bochs 或 SDL2 display 支持时必须失败并给出可操作错误；同时提供非 SDL2 的 `xmake run bochs` 后备入口。
- [Risk] `user_program_smoke` 与 `syscall_smoke` 同时开启时构建成功但 runtime marker 触发顺序互相短路。→ Mitigation: 本 change 只保证构建配置组合被保留并进入镜像；runtime marker 完整性由现有 smoke 设计和后续验证记录覆盖。

## Migration Plan

1. 在 `xmake.lua` 中增加 boot-stage artifact 构建目标或规则，保持现有输出路径和二进制大小限制。
2. 增加 `bochs-sdl2` 和 `bochs` runnable target，串起 kernel build、boot artifact build、raw image generation、generated bochsrc 和 Bochs launch。
3. 调整 `tools/boot_debug.py`，将 kernel build/config 逻辑拆分或移除，删除 smoke 快捷参数，并支持“消费已构建 artifacts”的 xmake 调用方式。
4. 删除根 `Makefile` 和 `src/arch/x86/boot/Makefile`，同步清理文档和测试引用。
5. 运行 `xmake`、`xmake f --user_program_smoke=y --syscall_smoke=y`、`xmake run bochs-sdl2`、`xmake run bochs` 或 Python helper no-launch 等价路径，记录 Bochs 环境不可用时的剩余风险。

Rollback 策略：如果 xmake boot-stage 构建迁移失败，可暂时保留 boot 子目录 Makefile 并让 xmake target 调用旧 Makefile 作为中间态，但最终归档前必须移除该中间态或在 tasks 中明确未完成。

## Open Questions

- 无。
