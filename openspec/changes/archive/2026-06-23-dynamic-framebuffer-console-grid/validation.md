## Validation

### Passed

- `openspec status --change "dynamic-framebuffer-console-grid" --json`
  - 结果：schema `spec-driven`，proposal/design/specs/tasks 均为 `done`。
- `openspec validate "dynamic-framebuffer-console-grid" --strict`
  - 结果：`Change 'dynamic-framebuffer-console-grid' is valid`。
- `uv run pytest tests/test_framebuffer_boot_handoff_source.py tests/test_tty_console_input_source.py`
  - 结果：21 passed。
  - 覆盖：backend-reported grid、VGA fixed 80x25、framebuffer dynamic grid 计算与 clamp、console dynamic visible rows/columns、PageUp/PageDown step、clear path、full framebuffer clear bounds、direct-map framebuffer 写入禁用。
- `xmake`
  - 结果：build ok。
- `clang++ -std=c++17 -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions -mno-sse -mno-sse2 -mno-mmx -mno-red-zone -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/terminal/console.cc`
  - 结果：通过。
- `clang++ -std=c++17 -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions -mno-sse -mno-sse2 -mno-mmx -mno-red-zone -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/terminal/console_render.cc`
  - 结果：通过。
- `xmake run qemu -- --display none --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 40`
  - 结果：通过，observed marker `BIGOS_USER_EXEC`。
  - 串口证据：`BIGOS_UEFI_FRAMEBUFFER base=0x0000000080000000 size=0x00000000003e8000 width=1280 height=800 stride=1280 format=1`、`BIGOS_UEFI_FONT ... cell=16x16`、`BIGOS_FONT_LOOKUP ready`、`BIGOS_CONSOLE_RENDER backend=framebuffer-text`。
  - 由串口几何和源码边界可推导该环境下 framebuffer backend 使用 80x50 visible grid；默认 UEFI/userland marker 未回归。

### Attempted / Historical Failures

- `uv run pytest`
  - 结果：290 passed, 20 failed。
  - 失败项集中在既有源码字符串/历史 OpenSpec 断言：缺失旧 active change `openspec/changes/add-metadata-consistency/proposal.md`、archive validation 中旧 `src/kernel` 文本、以及多个与当前 console/framebuffer 无关的 proc/VM/syscall 历史断言。
  - 本 change 相关目标测试已单独通过；这些全量失败未作为本 change blocker 处理。

### clangd

- `clangd --check=kernel/core/terminal/console.cc --compile-commands-dir=build`
- `clangd --check=kernel/core/terminal/console_render.cc --compile-commands-dir=build`
  - 结果：clangd 可用，但当前 compile database/include-cleaner 配置会把本地 `<bigos/...>` include 解析成空路径并报告 IncludeCleaner errors。
  - 替代检查：对两个修改 C++ 文件执行了贴近 freestanding x86_64 C++17 的 `clang++ -fsyntax-only`，并通过默认 `xmake` cross-toolchain build。

### Skipped / Residual Risk

- 未执行 QEMU + OVMF 图形化 framebuffer 截图/人工验证。
  - 原因：本次自动化只运行 headless QEMU serial marker；当前会话没有可确认的图形 display/screenshot 自动化证据链。
  - 已有替代证据：source-level tests、`xmake`、headless QEMU UEFI 串口证据确认 framebuffer metadata/font/backend selection 和默认 userland marker。
  - 剩余风险：full framebuffer clear、动态 rows/columns 的实际可见画面、软件光标和 scrollback viewport 仍需在可用图形环境中人工或截图验证。
