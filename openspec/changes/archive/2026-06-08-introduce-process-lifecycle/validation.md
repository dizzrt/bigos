## Validation Notes

### Passed Checks

- `openspec validate introduce-process-lifecycle --strict`: passed.
- `uv run pytest`: passed, `149 passed`.
- `xmake f --user_program_smoke=n --user_elf_smoke=n && xmake`: passed. The linker still reports the existing `build/kernel has a LOAD segment with RWX permissions` warning.
- `xmake f --user_program_smoke=y --user_elf_smoke=n && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/process-lifecycle-user-program.serial.log --expect-serial-marker BIGOS_USER_EXIT --smoke-timeout 20`: passed, serial marker observed.
- `xmake f --user_program_smoke=n --user_elf_smoke=y && uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/process-lifecycle-user-elf.serial.log --expect-serial-marker BIGOS_USER_EXIT --smoke-timeout 30`: passed, serial marker observed.

### Current-Change Diagnostics

- Source changes keep the fixed syscall vector `0x80`, IDT privilege split, higher-half layout, direct-map layout, `KVMEM_BASE`, and recursive self-map constants unchanged.
- `SYS_WAIT` is exposed as a minimal ABI, but the current `int 0x80` dispatch path remains a scheduler nonblocking context. Blocking child waits therefore return deterministic `WAIT_EWOULDBLOCK` from syscall dispatch until a later syscall-boundary design permits sleeping outside interrupt dispatch.
- General exec preparation supports bounded `argc` / `argv` / `envp` stack setup and rollback before publication. The current in-place `exec_current_from_elf_image` rejects active-root replacement rather than freeing an address space still active under CR3.

### Skipped Checks And Blockers

- Bochs cross-validation was not run in this pass. QEMU headless covered the ring3 user-program and user-ELF marker paths; residual Bochs-specific BIOS/PIO/port-IO behavior remains unverified.

### Residual Risk

- Blocking `wait` semantics are source-level only for contexts where `sched::can_block()` is true; user-visible blocking wait needs a future syscall entry redesign.
- In-place exec has no user syscall frontend yet and is intentionally conservative around active-root teardown.
