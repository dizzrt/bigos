## Context

BigOS 当前启动链路由多个手工步骤组成：`xmake` 生成内核 ELF，`src/arch/x86/boot/Makefile` 生成 MBR/DBR/exDBR/`boot.bin`，`tools/install.py` 将 boot 产物写入已有虚拟磁盘，最后通过 Bochs 启动。这个流程要求开发者预先准备 `test/bochsrc.bxrc` 和符合现有 bootloader 假设的 exFAT 镜像，导致启动调试依赖本机状态和手工操作。

本变更只解决第一阶段本地调试入口：通过一行命令完成构建、生成固定 raw disk image、写入 MBR/exFAT/`/boot/boot.bin`/`/kernel`，并启动 Bochs。它不改变 bootloader、内核 ELF、链接脚本或内核初始化逻辑。

当前 boot 链路的关键约束：

- BIOS 从 raw disk 的 MBR 启动。
- MBR 定位 active exFAT 分区并跳转 DBR。
- DBR 加载 extended DBR。
- extended DBR 在 exFAT 中查找 `/boot/boot.bin` 并加载到 `0x10000`。
- `boot.bin` 进入长模式后查找根目录 `kernel` 文件，按 ELF64 program header 加载高半区内核。
- 内核链接地址保持 `0xffffffff80000000`。

控制流：

```text
developer command
  |
  v
preflight tools
  |
  v
xmake kernel + boot Makefile
  |
  v
raw image builder
  |-- MBR
  |-- exFAT boot region + backup region
  |-- FAT / allocation bitmap / upcase placeholder as needed
  |-- root directory: kernel
  `-- /boot directory: boot.bin
  |
  v
bochs config/run
  |
  v
BIOS -> MBR -> DBR -> exDBR -> boot.bin -> kernel()
```

## Goals / Non-Goals

**Goals:**

- 提供一个稳定的一行命令，例如 `python3 tools/boot_debug.py run` 或 `make boot-debug`，完成本地启动调试准备和 Bochs 启动。
- 在用户态直接生成固定 raw disk image，不依赖 macOS `diskutil`、loop device、挂载权限、`mkfs.exfat` 或宿主机文件系统挂载。
- 生成满足现有 bootloader 支持范围的最小 exFAT 布局，保证 `/boot/boot.bin` 和 `/kernel` 连续存放。
- 复用现有 boot 构建产物和内核构建产物，不改变 boot handoff 协议。
- 在失败时给出明确阶段信息：环境缺失、构建失败、镜像生成失败、Bochs 缺失或 Bochs 启动失败。
- 允许开发者保留生成的 raw image 和 Bochs 配置，便于重复调试。

**Non-Goals:**

- 不实现 QEMU/headless/串口日志自动判定；这些属于后续阶段。
- 不实现完整通用 exFAT 文件系统库，只生成现有 bootloader 能读取的固定测试镜像布局。
- 不修改 `boot.s`、`boot.cc`、`link.lds`、`BootInfo` 布局或内核初始化顺序。
- 不修复当前内核构建中的历史编译问题；脚本应如实报告 `xmake` 失败。
- 不把 raw image 或本机 Bochs BIOS/VGA BIOS 二进制提交到仓库。
- 不保证跨架构启动，目标仍为 x86/x86_64 BIOS 启动。

## Decisions

### Decision: 默认第一阶段使用 Bochs

第一阶段默认模拟器使用 Bochs，因为项目现有 `xmake run kernel` 和顶层 `make run` 已围绕 Bochs，且 Bochs 对实模式、保护模式、长模式切换、GDT、分页和磁盘启动路径的调试更直观。

已有历史 Bochs 配置可作为参数参考，尤其是 `boot: disk`、`ata0-master` flat disk、32 MiB guest memory、单 CPU、x86_64 CPUID、`log: -` 和 `panic`/`error`/`info` 行为；但其中 `win32config`、`win32` display、Windows BIOS/VGA BIOS 路径和 Windows raw image 路径属于特定主机配置，不能被默认生成逻辑硬编码。

备选方案：

- QEMU：更适合 CI/headless 和串口自动判定，但这需要额外定义自动判定信号，超出第一阶段目标。
- VirtualBox/VMware：更接近真实虚拟机，但启动测试自动化和可观测性较差，不适合作为开发者一行命令入口。

### Decision: 脚本生成固定 raw disk image

脚本直接生成 raw image，并在二进制层面写入 MBR、exFAT boot region、目录项和文件内容。这样避免依赖宿主机挂载、管理员权限、平台特定磁盘设备和 `diskutil` 行为。

固定布局应显式写入：

- LBA 0：MBR 和一个 active exFAT partition entry。
- 分区起始 LBA：exFAT main boot region。
- 分区起始 LBA + 12：exFAT backup boot region。
- 固定 FAT/bitmap/root/boot directory/data cluster 区域。
- `/boot/boot.bin` 和 `/kernel` 以连续 cluster 存放，匹配当前 bootloader 对 contiguous 文件的要求。

备选方案：

- 维护模板镜像：实现更快，但模板容易过期，二进制资产不利于 review。
- 调用系统格式化和挂载工具：实现简单，但违反“不依赖 macOS diskutil/挂载权限”的目标。

### Decision: raw image builder 与 install helper 分工清晰

新的启动调试脚本可以复用 `tools/install.py` 的 MBR/DBR/exDBR/boot 写入逻辑，也可以将其内部能力抽象为可调用函数；但 image builder 必须负责创建初始 exFAT 布局和写入 `/kernel`。如果复用 `install.py`，需要保持 CLI 兼容，不破坏现有 `--with-mbr`、`--with-dbr`、`--with-exdbr` 和 `--with-boot` 参数。

备选方案：

- 完全合并为一个大脚本：调用更简单，但会弱化 install helper 的已有职责边界。
- 保持两个脚本完全独立：风险小，但重复解析 exFAT 布局逻辑。

### Decision: 一行命令可由 Makefile/xmake 包装

建议实现一个主 Python 脚本作为单一事实来源，再在顶层 `Makefile` 或 `xmake.lua` 增加短入口。例如：

```bash
python3 tools/boot_debug.py run
make boot-debug
```

Python 脚本负责阶段编排和错误信息，Makefile/xmake 只做薄包装，避免多处维护启动流程。

### Decision: Bochs 配置生成采用项目内可复现输出

脚本应生成或刷新 `build/test/bochsrc.bxrc` 之类的构建产物配置，指向生成的 raw image。仓库可提供模板或生成逻辑，但不应要求用户手工维护 `test/bochsrc.bxrc` 才能运行一行命令。

如果 Bochs 安装需要 host-specific BIOS/VGA BIOS 路径，脚本应优先使用 Bochs 默认配置能力；无法推断时提示用户传入配置路径或安装方式，而不是静默失败。

生成配置应从历史配置中抽取可移植的启动意图，而不是复制完整文件。建议默认配置包含：

- `memory: host=32, guest=32` 或可参数化的等价值。
- `boot: disk`。
- `ata0` enabled，`ata0-master` 指向生成的 raw image，`mode=flat`，`sect_size=512`。
- 单 CPU 和 x86_64 CPUID 能力。
- `log: -`，便于在终端观察 Bochs 日志。
- `com1: enabled=true, mode=null`，直到后续阶段引入串口自动判定。

生成配置不应默认包含：

- `config_interface: win32config` 或固定 `display_library: win32`。
- host-specific absolute ROM paths.
- host-specific absolute disk image paths.
- 要求交互的 `panic: action=ask`，除非用户显式选择交互调试模式。

## Risks / Trade-offs

- [Risk] 最小 exFAT 布局与 bootloader 的读取假设不一致，导致镜像生成成功但启动找不到 `boot.bin` 或 `kernel`。-> Mitigation: 将镜像布局常量集中定义，增加离线 layout validation，复用 `tools/install.py` 中的 exFAT 解析能力验证目录和连续文件。
- [Risk] Bochs 在不同平台的 BIOS/VGA BIOS 路径不同，一行命令仍可能受本机安装影响。-> Mitigation: 生成默认 Bochs 配置，preflight 明确检查 `bochs` 可用性，失败时输出可操作的配置参数说明。
- [Risk] 直接复用历史 `bochsrc.bxrc` 会带入 Windows-only 配置，在 macOS 或 Linux 上不可用。-> Mitigation: 只参考其可移植硬件意图，生成配置时替换 image path、display/config interface、ROM 路径和 panic 行为。
- [Risk] 当前 `xmake` 存在历史编译阻塞时，用户误以为启动脚本失败。-> Mitigation: 脚本阶段化输出，明确失败在 kernel build 阶段，并保留原始编译输出。
- [Risk] 直接生成 exFAT 容易演变成完整文件系统实现。-> Mitigation: 明确只支持固定镜像、固定目录、连续文件和现有 bootloader 需要的字段。
- [Risk] raw image 固定大小过小，后续 kernel 增大后写入失败。-> Mitigation: 支持脚本参数调整镜像大小，并在写入前检查 `boot.bin` 和 `kernel` 大小。
- [Risk] 一行命令自动覆盖调试镜像导致丢失手工排查现场。-> Mitigation: 默认写入 `build/test/` 下生成物，支持 `--keep-image` 或 `--image` 指定路径。

## Migration Plan

1. 新增启动调试脚本，先实现 preflight、build 阶段编排和清晰错误报告。
2. 实现 raw image builder，生成固定 MBR/exFAT 布局并写入 `/boot/boot.bin` 与 `/kernel`。
3. 接入或复用 MBR/DBR/exDBR/boot 写入逻辑，验证生成镜像能被现有 `tools/install.py` 解析。
4. 生成 Bochs 配置并启动 Bochs。
5. 增加顶层便捷入口和 README/README-zh 文档。
6. 用 boot 局部构建、镜像 layout validation 和 Bochs 可用时的人工 smoke test 验证。

Rollback 策略：本变更只新增开发工具入口和构建产物生成流程，不改变内核/boot runtime 行为；如启动脚本不可用，可删除新增入口并继续使用现有手工 `xmake`、boot Makefile、`tools/install.py` 和 Bochs 命令。

## Open Questions

- 一行命令最终名称选择 `make boot-debug`、`xmake run boot-debug` 还是 `python3 tools/boot_debug.py run` 作为主文档入口？
- Bochs 配置是否需要支持用户提供自定义 `bochsrc`，还是第一阶段只生成默认配置？当前倾向：默认生成配置，同时允许用户传入自定义配置或 ROM 路径覆盖 host-specific 项。
- raw image 默认大小选择多少更合适：64 MiB、128 MiB，还是根据 boot/kernel 大小动态向上取整？
