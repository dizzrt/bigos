## Context

当前 BigOS 的本地启动调试流程已经由 `tools/boot_debug.py` 统一负责构建前置检查、raw image 生成、exFAT 布局写入、Bochs 配置生成、QEMU/Bochs 启动和串口 marker 轮询。`xmake.lua` 目前提供 `bochs`、`bochs-sdl2`、`qemu`、`qemu-gdb` 四个 run target，但这些 target 都硬编码 helper 参数，不能转发 `xmake run <target> -- ...` 后面的 target arguments。

显示模式当前有两套表达方式：Bochs 通过 `bochs-sdl2` target 名称隐式选择 `display_library: sdl2`，QEMU 通过 helper 的 `--display graphical|none` 参数选择图形或无头。该差异让命令形态不一致，也让 Bochs no-GUI 等本地调试能力无法自然复用 xmake 入口。

本 change 只改开发工具链与文档，不修改 boot sector、DBR、extended DBR、`boot.bin`、kernel ELF 加载规则、ATA PIO driver、kernel handoff、linker 地址、GDT/IDT、page table、interrupt vector 或内核初始化顺序。

目标命令形态：

```text
xmake run bochs
        |
        +--> tools/boot_debug.py run --emulator bochs --display sdl2

xmake run bochs -- --display none
        |
        +--> tools/boot_debug.py run --emulator bochs --display none

xmake run qemu -- --display none
        |
        +--> tools/boot_debug.py run --emulator qemu --display none
```

## Goals / Non-Goals

**Goals:**

- 将 `bochs-sdl2` target/backend 收敛为单一 `bochs` target/backend。
- 让 `xmake run bochs` 默认使用 SDL2 display。
- 让 `xmake run bochs -- --display sdl2|none`、`xmake run qemu -- --display graphical|none` 和 `xmake run qemu-gdb -- --display graphical|none` 通过统一 helper 参数表达 display 模式。
- 让 `xmake run bochs|qemu|qemu-gdb -- ...` 转发 target arguments 到 `tools/boot_debug.py run`，并保留 xmake 对 kernel 与 boot artifacts 的构建职责。
- 保持 QEMU GDB stub、serial log、serial marker smoke、no-launch helper、Bochs generated config 和 raw image 生成语义。
- 更新测试、README、README-zh、AGENTS、docs/en、docs/zh 和 OpenSpec specs。

**Non-Goals:**

- 不实现 `bochs-gdb`、Bochs GDB stub 或 `bochsdbg` target。
- 不把 QEMU/Bochs display 后端能力检测做成完整跨平台探测系统；不可用的 display library 由 emulator 启动失败信息和文档说明承接。
- 不新增 UEFI、OVMF、ESP/FAT、virtio、AHCI/SATA、NVMe 或其它存储/启动后端。
- 不改变 image layout、boot protocol、kernel ABI、linker address、BootInfo、smoke marker ABI 或内核运行时初始化顺序。
- 不将 `--no-launch` 作为 xmake run target 的稳定参数；离线 image 生成仍通过 Python helper 直接调用。

## Decisions

### Decision: helper 拥有 display 语义，xmake 只负责参数转发

`xmake.lua` 的 `bochs`、`qemu`、`qemu-gdb` target 应只拼接固定 wrapper 参数和 `option.get("arguments")` 中的 target arguments，然后使用 `os.execv("python3", args)` 调用 helper。`xmake.lua` 不解析 `--display` 或其它 helper 参数，避免 shell quoting 问题和 emulator 细节扩散。

备选方案是在 `xmake.lua` 中直接识别 display 参数并生成不同 emulator 命令。该方案会让 display 校验、默认值、serial log、marker、QEMU extra args 和 Bochs config 逻辑分散在 Lua 与 Python 两处，因此不采用。

### Decision: 取消 `bochs-sdl2`，`bochs` 默认 SDL2

`bochs-sdl2` 表达的是 display 选择，不是独立 emulator backend。收敛后，`bochs` 代表 Bochs backend，`--display sdl2` 代表 SDL2 图形显示，`xmake run bochs` 默认等价于 `xmake run bochs -- --display sdl2`。

备选方案是保留 `bochs-sdl2` 作为兼容 alias。该方案降低迁移成本，但继续保留两套 display 表达方式；本 change 明确接受破坏性入口调整，文档和 specs 同步删除旧入口。

### Decision: display 参数按 emulator 校验

`--display` 成为 helper 的通用参数，但允许值按 backend 校验：

- `bochs`: `sdl2`、`none`，默认 `sdl2`。
- `qemu` / `qemu-gdb`: `graphical`、`none`，默认 `graphical`。

Bochs `sdl2` 映射为 `display_library: sdl2`，Bochs `none` 映射为 `display_library: nogui` 或当前 Bochs 支持的等价 no-GUI 配置。QEMU `none` 映射为 `-display none`，QEMU `graphical` 保持当前默认图形显示。

备选方案是定义全局枚举 `graphical|sdl2|none` 并允许所有 emulator 接收。该方案容易产生无效组合，例如 `qemu --display sdl2`，因此采用 backend-aware 校验。

### Decision: 保持 generated artifact 隔离

移除 `bochs-sdl2` 后，Bochs 的默认 serial log 使用 `build/test/bochs.serial.log`，QEMU 继续使用 `build/test/qemu.serial.log`，QEMU GDB 继续使用 `build/test/qemu-gdb.serial.log`。生成的 `bochsrc`、raw image、serial logs 仍位于 `build/test` 或显式指定路径下。

## Risks / Trade-offs

- `xmake run bochs-sdl2` 被删除会破坏已有个人脚本或文档习惯 -> README、README-zh、AGENTS、docs 和 OpenSpec 全量更新，并在 proposal 中标记 breaking change。
- Bochs `display_library: nogui` 可能在部分安装中不可用 -> 文档记录本机 Bochs display library 依赖；preflight 仍只检查 `bochs` 可执行程序，启动失败必须暴露 emulator 输出。
- xmake target argument 转发可能引入 quoting 风险 -> 使用 `os.execv` 参数数组而不是 shell 字符串拼接。
- display 默认值从 argparse 静态默认迁移到 backend-aware 默认可能引入行为差异 -> 用 parser 和 `qemu_command`/`render_bochsrc` 单元测试覆盖默认值、显式值和非法组合。

## Migration Plan

- 先扩展 `tools/boot_debug.py`：删除 `bochs-sdl2` backend，增加 backend-aware display 默认值和校验，支持 Bochs `sdl2/none`，保持 QEMU `graphical/none`。
- 再改造 `xmake.lua`：删除 `target("bochs-sdl2")`，为 `bochs`、`qemu`、`qemu-gdb` 使用 `os.execv` 并转发 `option.get("arguments")`。
- 然后更新测试：覆盖 parser、参数转发、Bochs generated config、QEMU display 和非法组合。
- 最后更新 README、README-zh、AGENTS、docs/en、docs/zh 和 OpenSpec specs。
- 回滚策略：如新 Bochs display 语义出现阻塞，可临时恢复 `bochs-sdl2` target 作为 alias，但仍建议保留 helper 的通用 `--display` 参数模型。

## Open Questions

- 暂无。
