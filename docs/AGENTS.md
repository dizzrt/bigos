# Agent Guide For BigOS Docs

This subtree contains bilingual project documentation. `docs/en` is canonical; `docs/zh` is the Simplified Chinese mirror.

## Documentation Rules

- Consult `docs/en/...` first for architecture, KTL, validation, and subsystem facts.
- Use `docs/zh/...` only as the synchronized Simplified Chinese mirror or to clarify translation consistency.
- Keep `docs/en` and `docs/zh` Markdown file sets isomorphic: every Markdown file below one language root must have the same relative path below the other.
- When updating technical facts, paths, markers, ABI details, build flags, validation notes, or non-goals, update both language versions in the same change.
- Treat the current bounded userland baseline as multi-core capable and tied to
  the x86_64 UEFI default backend, with the Legacy BIOS/MBR/exFAT path retained
  as an explicit compatibility/debug backend: default PID-1 init, `/bin/sh`,
  bounded POSIX-like process/I/O behavior, demand paging, bounded `fork`/COW,
  signals, time/identity, bounded `/rw`, persistent writable storage,
  constrained rename, metadata, cwd/relative paths, pipe/dup, minimal user
  crt0/libc, bounded FILE streams, bounded dynamic linking, bounded UDP socket
  support, static packaged programs, and behavior-oriented validation. Keep
  non-goals explicit for full POSIX coverage, complete job control, complete
  terminal process groups, broad writable file-backed `mmap`, broad user-visible
  async I/O, CPU hotplug, NUMA, broad storage/device support, complete
  journaling/crash recovery, multi-architecture runtime parity, and broad
  backend parity.
- Keep `roadmap.md` limited to project-level implemented capabilities, missing
  capabilities, medium/long-term planning, and staged development priorities.
  Do not put concrete entry points, file paths, commands, validation markers,
  implementation details, or archive/version indexes in the roadmap.
- Do not cite roadmap stage numbers or roadmap task numbers in documentation
  updates, OpenSpec change notes, or archive notes. Use capability names,
  behavior names, implementation boundaries, or artifact-local checklist numbers
  instead.
- Use repository-relative documentation references such as `docs/en/arch/syscall-entry.md`; do not write machine-specific absolute paths.
- Do not recreate the removed top-level architecture or KTL documentation roots; the language roots are the only active documentation locations.
