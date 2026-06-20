## Validation Notes

本 change 的实现边界已覆盖受控 AP startup/per-CPU timer 基线的代码路径，并已完成本地 QEMU headless 与 Bochs SDL2 多核验证。

已执行检查：

- `xmake f --ap_startup_percpu_timers=y && xmake`：通过。
- `xmake f --ap_startup_percpu_timers=n && xmake`：通过，默认关闭配置仍保持 BSP-only 构建路径。
- `openspec validate add-ap-startup-percpu-timers --strict`：通过。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/apic-serial.log --expect-serial-marker BIGOS_AP_LOCAL_TIMER --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 30 --qemu-extra "-cpu max -smp 2"`：通过；serial log 观察到 `BIGOS_ACPI_MADT_DONE`、`BIGOS_LAPIC_X2APIC_READY`、`BIGOS_AP_LOCAL_TIMER_READY`、`BIGOS_AP_ONLINE`、`BIGOS_AP_LOCAL_TIMER` 和 `BIGOS_USER_EXEC`。
- `xmake run bochs -- --display sdl2 --bochs-cpus 2 --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 20`：通过；serial log 观察到 `BIGOS_AP_LOCAL_TIMER_READY` 和 `BIGOS_USER_EXEC`，未再出现 `fork failed`、`execve /bin/sh failed`、`BIGOS_PANIC` 或 `BIGOS_PAGE_FAULT`。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/qemu-smp-post-bochs.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 20 --qemu-extra "-cpu max -smp 2"`：通过，用于复核 Bochs 修复未回退 QEMU 多核路径。
- 本地默认 QEMU CPU model 的 `-smp 2` 配置仍会在 AP 侧 xAPIC MMIO LAPIC 写路径卡住；通过 `-cpu max -smp 2` 暴露 x2APIC 后，BSP 保持先用 xAPIC/MMIO 完成 PIT 参考校准，再切换到 x2APIC，AP 侧使用 MSR 路径完成 LAPIC enable 和 local timer 配置。
- Bochs 多核修复确认了 AP LAPIC timer interrupt 不得进入 BSP-only scheduler 的 `NonblockingContextGuard` 或 IRQ-return preemption bridge；AP timer 只运行 AP-local handler 并发送 LAPIC EOI。

实现范围说明：

- 默认构建不启用 `BIGOS_AP_STARTUP_PERCPU_TIMERS`，继续保持 BSP-only PIT/i8259 默认路径。
- 启用 `ap_startup_percpu_timers` 后，内核会准备 AP trampoline/mailbox、初始化 LAPIC、尝试基于 MP table 或 ACPI MADT fallback 启动 AP，并在失败时 fail closed；在本地 QEMU `-cpu max -smp 2` 和 Bochs `--bochs-cpus 2` 验证配置下，AP 能完成 online ack 与 local timer tick。
- AP online 前不会暴露给 scheduler、timer 或用户态路径；AP timer tick 只记录 CPU-local tick，不迁移 runnable work，也不执行跨 CPU wakeup。
- 当前 change 不包含 per-CPU run queue、IPI TLB shootdown、CPU hotplug 或完整 APIC 默认中断投递。

残余风险：

- 本地 QEMU 多核配置未提供 MP table 拓扑，已通过 ACPI MADT fallback 完成验证；MP table 固件环境仍需要在可用平台上补充实机或替代固件交叉验证。
- 默认 QEMU CPU model 下的 xAPIC MMIO AP local timer 路径仍不是本次通过的验证配置；本次 6.4 覆盖的是显式 `-cpu max -smp 2` 的 x2APIC AP local timer 路径。
- 本地 Bochs `--display none` 的可用性取决于安装包是否提供 `nogui` display plugin；当前通过的 Bochs 自动 smoke 使用 `--display sdl2`。
