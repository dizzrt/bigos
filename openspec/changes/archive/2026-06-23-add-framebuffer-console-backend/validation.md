# Validation Notes

## 已执行

- `openspec status --change add-framebuffer-console-backend --json && openspec validate add-framebuffer-console-backend --strict`
  - 结果：通过，change artifacts 可被 OpenSpec 识别。
- `uv run pytest tests/test_tty_console_input_source.py tests/test_device_driver_framework_source.py tests/test_framebuffer_boot_handoff_source.py tests/test_kernel_glyph_font_source.py`
  - 结果：27 passed。
  - 覆盖：runtime console state 与 render backend 边界、VGA fallback 适配、framebuffer backend 通过 `map_device_mmio()` 写入、glyph lookup 仍保持只读输入层。
- `xmake`
  - 结果：通过，默认 kernel 构建成功。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -mno-red-zone -fno-rtti -fno-exceptions -I include -I cpp/include -I cpp/libsupc++/include -fsyntax-only kernel/core/terminal/console.cc kernel/core/terminal/console_render.cc`
  - 结果：通过，新增/修改 terminal C++ 源文件完成近似 freestanding C++17 语法检查。
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display none --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi-framebuffer-console.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，观察到 `BIGOS_USER_EXEC`。
  - 关键串口证据：`BIGOS_UEFI_FRAMEBUFFER base=0x0000000080000000 size=0x00000000003e8000 width=1280 height=800 stride=1280 format=1`、`BIGOS_UEFI_FONT ... cell=16x16`、`BIGOS_FONT_LOOKUP ready`、`BIGOS_CONSOLE_RENDER backend=framebuffer-text`。
- `uv run python tools/boot_debug.py run --boot-mode uefi --emulator qemu --display graphical --image build/test/uefi-esp.img --uefi-root-image build/test/uefi-root.raw --serial-log build/test/qemu-uefi-framebuffer-console-graphical.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，观察到 `BIGOS_USER_EXEC`。
  - 关键串口证据：同样记录 `1280x800` GOP framebuffer、glyph lookup ready、`BIGOS_CONSOLE_RENDER backend=framebuffer-text`。
- `uv run python tools/boot_debug.py run --boot-mode legacy --emulator qemu --display none --image build/test/os.raw --serial-log build/test/qemu-legacy-framebuffer-console-fallback.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，观察到 `BIGOS_USER_EXEC`。
  - 关键串口证据：`BIGOS_CONSOLE_RENDER backend=vga-text`，确认 Legacy BIOS fallback 不依赖 framebuffer metadata 或 glyph lookup。

## Skipped / Residual Risk

- QEMU/OVMF graphical 启动已执行到 marker，但本记录未采集截图，也未进行人工键盘 PageUp/PageDown/Home/End 操作；因此“可见文本、软件光标像素形态、scrollback viewport 导航”的人工图形证据仍为未采集风险。
- 该 change 未修改 BootInfo v2 ABI、kernel link address、page-table layout、IDT/syscall vector、磁盘布局或用户态 syscall ABI；验证主要覆盖源码边界、默认构建、UEFI framebuffer backend selection 和 Legacy VGA fallback。
- framebuffer backend 只渲染当前 runtime console `char` cells；它不声明 UTF-8 decoding、CJK 显示、Unicode codepoint cell、双宽 cell、ANSI/VT、`termios`、多终端或完整 POSIX terminal。
