# BigOS Documentation

`docs/en` is the canonical English documentation tree for BigOS. `docs/zh` is the Simplified Chinese mirror with the same relative Markdown paths and equivalent technical content.

Current project-level status lives in `README.md`, `README-zh.md`, `AGENTS.md`,
`openspec/config.yaml`, and `roadmap.md`. Architecture docs should describe the
current minimal usable system baseline as a multi-core capable x86_64 kernel
moving toward a general-purpose, POSIX-compatible Unix-like system. The x86_64
UEFI backend is the default runnable baseline; the Legacy BIOS/MBR/exFAT path
remains an explicit compatibility and debug backend. Do not overstate current
implementation coverage as complete POSIX, complete network/storage, or
release-grade general-purpose behavior; describe such areas as staged
compatibility-expansion work.

`roadmap.md` is for project-level implemented capabilities, missing capabilities,
medium/long-term planning, and staged development priorities. Keep concrete
entry points, file paths, commands, validation markers, implementation details,
and archive/version indexes in dedicated documentation or change records.
Filesystem and userland documentation must keep bounded `/rw`, persistent
storage, constrained rename, metadata, cwd/relative paths, pipe/dup, bounded
socket support, bounded dynamic linking, minimal libc, and static user programs
separate from full POSIX filesystem, complete journaling/crash recovery, broad
user-visible async I/O, complete job control, complete POSIX libc, and broad
storage/device support. Those broader capabilities are future compatibility
targets unless a roadmap stage explicitly declares them out of scope.

## Language Entry Points

- English canonical docs: `docs/en`
- Simplified Chinese mirror: `docs/zh`

## Main Sections

- English architecture docs: `docs/en/arch`
- English KTL docs: `docs/en/ktl`
- 简体中文架构文档：`docs/zh/arch`
- 简体中文 KTL 文档：`docs/zh/ktl`

Use repository-relative paths when linking to documentation. Do not use machine-specific absolute paths for repository docs.
