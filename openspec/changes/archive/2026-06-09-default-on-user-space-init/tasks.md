## 1. 实现 launch_init 内核入口

- [x] 1.1 在 `kernel/core/proc/`（或 kernel.cc 内）新增无 `#ifdef` 守卫的 `launch_init`，复用 `vfs::init` -> `open_absolute("/boot/user/init.elf")` -> bounded 读入 -> `create_elf_user_process` -> `run_user_process`
- [x] 1.2 在 `kernel()` 中于 `bigos::proc::init()` 之后、`bigos::sched::start()` 之前调用 `launch_init`，且不被任何 smoke `#ifdef` 包裹
- [x] 1.3 进入 ring3 前发出 `BIGOS_INIT_ENTER` 串口 marker
- [x] 1.4 引入中性常量 `INIT_ELF_PATH` 指向 `/boot/user/init.elf` 供 `launch_init` 使用，保留 `USER_ELF_SMOKE_PATH` 不变（决策 3）

## 2. init 缺失/非法降级与退出语义

- [x] 2.1 实现缺失/超过 `USER_ELF_MAX_FILE_BYTES`/非法 ELF 的确定性降级：发 `BIGOS_INIT_*`（含失败原因）后进入统一 panic 路径
- [x] 2.2 实现 init 正常退出语义：发出 `BIGOS_INIT_EXIT` marker 后进入现有 idle 调度（halt），而非 panic（决策 5）
- [x] 2.3 确认 init 退出后复用现有 zombie/reaper teardown，不泄漏地址空间、不破坏调度器不变量

## 3. 构建与镜像打包

- [x] 3.1 修改 `xmake.lua` 使 `user-init-elf` target 在默认构建中 `set_default(true)`，不再以 `user_elf_smoke` 为条件
- [x] 3.2 确认默认构建的磁盘镜像安装流程包含 `/boot/user/init.elf`，且 `user_elf_smoke` 仍复用同一产物
- [x] 3.3 验证 `user_program_smoke` / `user_elf_smoke` 开关、其 `BIGOS_USER_*` marker 与矩阵用例保持不变

## 4. 验证与行为断言

- [x] 4.1 在 Stage 9 运行时 smoke 矩阵新增默认构建（无 smoke 开关）init case，断言 `BIGOS_INIT_ENTER` / `BIGOS_INIT_EXIT`
- [x] 4.2 用 QEMU headless 默认构建跑通并采集串口 marker（`uv run python tools/boot_debug.py run --emulator qemu --display none ...`）
- [x] 4.3 新增/调整行为断言测试，本 change 仅断言内核 `BIGOS_INIT_*` marker；init 二进制 stdout 输出断言留待后续阶段（决策 6）
- [x] 4.4 源码契约断言 `launch_init` 及其调用点无 `#ifdef` 守卫

## 5. 文档同步

- [x] 5.1 更新 `docs/en` 与 `docs/zh` 对应文档，描述默认 init 启动路径、`BIGOS_INIT_*` marker 与新失败模式（保持目录同构、使用仓库相对路径）
- [x] 5.2 在 `roadmap.md` 中将 Stage 14.5 状态由 proposed 更新为进行中/已实现（落地后）
- [x] 5.3 运行 `openspec validate default-on-user-space-init` 并记录验证/跳过原因与残留风险
