# 验证记录

## 已通过

- `uv run pytest tests/test_first_user_program_source.py tests/test_syscall_entry_source.py tests/test_user_address_space_vmem_source.py`
  - 结果：`30 passed`
- `uv run pytest`
  - 结果：`102 passed`
- `xmake`
  - 默认关闭 `user_program_smoke` 配置下构建通过，默认 boot 构建行为保持不创建用户进程。
- `xmake f --user_program_smoke=y && xmake`
  - smoke 构建通过，确认 `BIGOS_USER_PROGRAM_SMOKE` 路径可编入。
- `openspec validate load-first-user-program --strict`
  - 结果：`Change 'load-first-user-program' is valid`
- IDE diagnostics
  - 结果：无诊断错误。

## 未通过 / 无法确认

- `xmake f --user_program_smoke=y && uv run python tools/boot_debug.py run --serial-log build/test/user_program_serial.log --expect-serial-marker BIGOS_USER_EXIT --smoke-timeout 10`
  - 结果：kernel、boot artifacts 和 raw image 构建完成，Bochs 启动后等待 `BIGOS_USER_EXIT` 超时。
  - 观察：`build/test/bochsrc.bxrc` 已配置 `com1` 输出到 `build/test/user_program_serial.log`，但该 serial log 文件未生成。
  - 剩余风险：当前环境的 Bochs/serial oracle 未能提供 marker 证据，因此 ring3 runtime bootability 仍需在可观测 serial/VGA 环境中复验。

## 配置恢复

- Bochs 尝试后已运行 `xmake f --user_program_smoke=n && xmake`，恢复默认关闭配置并确认默认构建通过。
