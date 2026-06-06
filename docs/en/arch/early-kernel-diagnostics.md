# Early Kernel Diagnostics And Panic

BigOS provides one freestanding-safe early fatal diagnostic facility. It consolidates fatal halt paths from mm (buddy/slab/kmem/self-test) and irq (exceptions/`#PF`) behind one entry point, guarantees a consistent "emit diagnostics -> disable interrupts -> halt safely" order, and unifies fatal marker prefixes.

## Entry Points

- Header: `include/bigos/panic.h`; implementation: `src/kernel/bigos/panic.cc`; namespace: `bigos`.
- `bigos::khalt()`: unified halt primitive. It first disables maskable interrupts with `cli`, then enters a `hlt` loop. It is `[[noreturn]]` and is used by paths that already emitted their diagnostic marker.
- `bigos::kpanic(code, source[, fmt, ...])`: emits a fixed first-line marker through constant strings, `BIGOS_PANIC code=<code> source=<source>` (COM1 + VGA), optionally emits formatted context, then halts through `khalt()`.
- `bigos::kpanic_with_mm_stats(code, source)`: optional diagnostic snapshot variant that reuses read-only `print_slab_stats()` before halt without allocating memory or triggering paths that might fail again.

The entry points do not depend on heap allocation, exceptions, RTTI, scheduler, IRQ-context services, or hosted runtime APIs.

## Markers And Error Codes

- Fixed first line: `BIGOS_PANIC code=<code> source=<source>`, using the same space-separated `key=value` style as existing `BIGOS_EXCEPTION`; `code` is printed in hexadecimal and `source` is a stable source identifier.
- Error codes come from the stable `bigos::PanicCode` enum and are grouped by source domain: mm-buddy, mm-arena, mm-slab, mm-vmem (reserved), self-test, irq-exception, irq-pagefault, and generic. Early boot-stage domains are reserved by design and are not wired into this facility.

## Existing Marker Compatibility

Migration preserves existing diagnostic output contracts for each path and only adds `BIGOS_PANIC` to fatal paths that previously lacked a prefix:

- `BIGOS_EXCEPTION`, `BIGOS_PAGE_FAULT`: exception/`#PF` diagnostic lines are unchanged; halt goes through the unified primitive.
- `BIGOS_MM_SELF_TEST_FAILED stage=<stage>`: self-test failure output is unchanged; halt goes through `khalt()`.
- Buddy handoff failures, early metadata arena exhaustion, and `BIGOS_SLAB_DEBUG` guard failures now go through `kpanic`, emitting a `BIGOS_PANIC` prefix and stable error code.

## Integration Scope

Only kernel runtime and mm/irq paths are covered. Early boot code (`src/arch/x86/boot/*`) is not integrated into this facility and keeps its existing failure/halt behavior. The idle `hlt` loop at the end of `kernel()` is a normal non-fatal halt path and is not affected.
