## Validation

### Passed

- `xmake f -y && xmake -y`: passed.
- `xmake f -y --journaled_rw_smoke=y && xmake -y`: passed.
- `/opt/homebrew/opt/llvm/bin/clang++ -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -target x86_64-elf -nostdinc++ -Iinclude -Icpp/include -Icpp/libsupc++/include -Ikernel -I. -DBIGOS_USER_PROCESS -fsyntax-only kernel/core/fs/bigfs.cc kernel/core/fs/bcache.cc kernel/core/kernel.cc`: passed.
- `openspec validate add-rw-write-ahead-journal --strict`: passed.

### Checked Boundaries

- Persistent BigFS format is now journal-capable v3 with a fixed 32-block journal region and a reduced 213-block data region.
- Persistent `/rw` metadata/data mutations use a journal-first ordered path: descriptor/payload blocks, commit marker, home-location blocks, then checkpoint/clear marker.
- Mount validation accepts only checkpoint-clean journal state; committed but uncheckpointed and partial journal states are rejected for persistent `/rw` publication.
- RAM-backed `/rw` remains current-session-only; read-only exFAT boot assets remain isolated from journal layout changes.
- `journaled_rw_smoke` is default-off and records M15.1 journal-first validation without claiming mount-time replay/discard recovery.

### Not Runtime-Run

- QEMU/Bochs serial-marker runs were not executed in this session.
- Runtime fault-injection for payload write failure, commit marker failure, home-location write failure, and checkpoint failure was not executed; the implementation paths return deterministic errors and preserve dirty/pending state, but emulator-level failure injection remains residual risk.
- Mount-time replay/discard recovery is intentionally out of scope for M15.1.

### clangd

- `/opt/homebrew/opt/llvm/bin/clangd --check=kernel/core/fs/bigfs.cc --compile-commands-dir=.` completed AST construction but exited 3 due to the known `ExtractFunction` tweak diagnostic: `Cannot extract break/continue without corresponding loop/switch statement`. No source syntax diagnostic was observed.
