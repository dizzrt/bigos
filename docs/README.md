# BigOS Documentation

`docs/en` is the canonical English documentation tree for BigOS. `docs/zh` is the Simplified Chinese mirror with the same relative Markdown paths and equivalent technical content.

Current project-level status lives in `README.md`, `README-zh.md`, `AGENTS.md`,
`openspec/config.yaml`, and `roadmap.md`. Architecture docs should describe the
current compressed stages 20-44 baseline as a single-core, mostly synchronous,
x86_64 research kernel with a bounded POSIX-like subset. Legacy
BIOS/MBR/exFAT remains the default runnable baseline; the x86_64 UEFI boot
backend is a runnable non-parity spike. Do not describe BigOS as complete POSIX
coverage or a complete general-purpose OS.

`roadmap.md` is for project-level implemented capabilities, missing capabilities,
medium/long-term planning, and staged development priorities. Keep concrete
entry points, file paths, commands, validation markers, implementation details,
and archive/version indexes in dedicated documentation or change records.
Filesystem and userland documentation must keep bounded `/rw`, persistent
clean-sync storage, constrained rename, metadata, cwd/relative paths, pipe/dup,
minimal libc, and static user programs separate from full POSIX filesystem,
journaling/crash recovery, async I/O, dynamic linking, job control, complete
POSIX libc, and broad storage/device support.

## Language Entry Points

- English canonical docs: `docs/en`
- Simplified Chinese mirror: `docs/zh`

## Main Sections

- English architecture docs: `docs/en/arch`
- English KTL docs: `docs/en/ktl`
- 简体中文架构文档：`docs/zh/arch`
- 简体中文 KTL 文档：`docs/zh/ktl`

Use repository-relative paths when linking to documentation. Do not use machine-specific absolute paths for repository docs.
