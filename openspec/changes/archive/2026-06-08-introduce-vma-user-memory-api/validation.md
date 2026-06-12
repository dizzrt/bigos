## Validation Notes

- `uv run pytest tests/test_vma_user_memory_api_source.py`: passed, 8 source-level VMA/API checks.
- `uv run pytest tests/test_vma_user_memory_api_source.py tests/test_user_address_space_vmem_source.py tests/test_process_lifecycle_source.py tests/test_syscall_entry_source.py tests/test_address_space_lifecycle_source.py`: passed, 38 checks.
- `xmake`: passed for the default kernel configuration; linker still reports the existing `build/kernel has a LOAD segment with RWX permissions` warning.
- `clang++ -std=c++17 -target x86_64-unknown-none -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/proc/proc.cc kernel/core/syscall/syscall.cc kernel/core/irq/interrupt.cc kernel/mm/vmem.cc`: passed.
- clangd diagnostics through the IDE diagnostics API: passed for `include/bigos/proc.h`, `include/bigos/syscall.h`, `kernel/core/proc/proc.cc`, `kernel/core/syscall/syscall.cc`, `kernel/core/irq/interrupt.cc`, `kernel/mm/vmem.h`, and `kernel/mm/vmem.cc`.
- `openspec validate introduce-vma-user-memory-api --strict`: passed.
- `xmake f --user_program_smoke=y && xmake`: passed for the ring3 first-user-program configuration.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/user_vma_user_program.serial.log --expect-serial-marker BIGOS_USER_WRITE_SYSCALL --expect-serial-marker BIGOS_USER_EXIT`: passed; covers VMA-backed syscall user-buffer validation through the first user program.
- `uv run python tools/boot_debug.py run --emulator bochs --display none --skip-build --serial-log build/test/user_vma_user_program.bochs.serial.log --expect-serial-marker BIGOS_USER_EXIT`: passed; cross-validates the same user process exit path under Bochs.
- Residual risk: stack-growth recovery is gated by `sched::can_block()` and therefore remains disabled from the current exception nonblocking path; non-stack user faults still terminate deterministically. Full runtime stack-growth success needs a later safe fault-allocation context or a dedicated smoke that enters an allocatable gate.
