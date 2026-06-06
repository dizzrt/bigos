# Agent Guide For BigOS Docs

This subtree contains bilingual project documentation. `docs/en` is canonical; `docs/zh` is the Simplified Chinese mirror.

## Documentation Rules

- Consult `docs/en/...` first for architecture, KTL, validation, and subsystem facts.
- Use `docs/zh/...` only as the synchronized Simplified Chinese mirror or to clarify translation consistency.
- Keep `docs/en` and `docs/zh` Markdown file sets isomorphic: every Markdown file below one language root must have the same relative path below the other.
- When updating technical facts, paths, markers, ABI details, build flags, validation notes, or non-goals, update both language versions in the same change.
- Use repository-relative documentation references such as `docs/en/arch/syscall-entry.md`; do not write machine-specific absolute paths.
- Do not recreate the removed top-level architecture or KTL documentation roots; the language roots are the only active documentation locations.
