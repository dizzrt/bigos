# User Address-Space Page Table Preparation

This stage abstracts the page-table code in `src/mm/vmem.cc`, previously limited to kernel ranges, into explicit map/unmap primitives. It defines user/kernel page-attribute policy and minimal user address-space root derivation. Default helpers still do not switch CR3 implicitly; the default-off first-user-program runtime path explicitly activates the derived root before entering ring3.

## Explicit Page-Attribute Primitives

The stage adds `bigos::mm::PageAttr` and `page_attr` constants in `include/bigos/memory.h`. Their bits match x86_64 paging-structure entries and can be ORed directly into physical frame addresses:

| Name | bit | Meaning |
| --- | --- | --- |
| `PRESENT` | 0 | Entry is valid |
| `WRITABLE` | 1 | Writable |
| `USER` | 2 | User-mode accessible |
| `GLOBAL` | 8 | Global page |
| `NO_EXECUTE` | 63 | Non-executable, requires EFER.NXE |

Core primitives:

- `bool map_page(uint64_t vaddr, uint64_t phys, PageAttr attr)`: creates one 4 KiB mapping, reusing recursive self-mapping traversal and missing-level page-table allocation. It rolls back newly created intermediate levels and returns `false` on missing-level allocation failure. For user mappings, intermediate entries inherit the user bit so the leaf is reachable; NX is encoded only in the leaf PTE.
- `void unmap_page(uint64_t vaddr)`: clears the PTE and executes `invlpg` for that address, matching the invalidation semantics of `rollback_kernel_range()`.
- `bool map_page_in_root(uint64_t root, uint64_t vaddr, uint64_t phys, PageAttr attr)`: edits the low-half page tables of the specified derived root through the direct map without writing CR3, allowing the user-program loader to populate code/data/BSS/stack while the kernel address space remains active.
- `bool user_range_mapped(uint64_t root, uint64_t vaddr, uint64_t len)`: verifies that a bounded user range is below the canonical user half and that every page has present/user PTEs. It does not implement demand paging.

These primitives are non-interrupt-context-only. They use `InterruptGuard` while writing entries to mask same-CPU maskable IRQ interleaving, must not be called from IRQ handlers, and must not trigger dynamic allocation inside IRQ handlers.

## User / Kernel Attribute Policy

| Purpose | Constant | Bit combination | Relation to old `0x3` |
| --- | --- | --- | --- |
| Kernel default | `KERNEL_DEFAULT` | `PRESENT | WRITABLE` | Bit-for-bit equivalent to old `DEFAULT_ATTR_PTE = 0x3` (`user=0`, `NX=0`) |
| User data page | `USER_DATA` | `PRESENT | WRITABLE | USER | NO_EXECUTE` | Sets user bit and encodes NX |
| User code page | `USER_CODE` | `PRESENT | USER` | Sets user bit and clears NX |

`VMem::map_kernel_range()` / `unmap_kernel_range()` now express kernel mappings through `map_single_page()` with `KERNEL_DEFAULT`. PTE bits remain exactly equivalent to the old `0x3`, and kernel ranges are not marked user-accessible. Source-level tests assert this equivalence.

## EFER.NXE State And NX Degradation

The current long-mode entry path in `src/arch/x86/boot/boot.s` sets only `LME` (bit 8) in `IA32_EFER` (MSR `0xc0000080`), and **does not enable NXE (bit 11)**. Therefore, the NX bit is currently mainly an attribute-encoding check and must not be relied on for runtime non-executable enforcement. The first user program still maps data/BSS/stack as `USER_DATA`, but runtime NX enforcement is left to a later enable-NXE change.

**Remaining risk**: if future code incorrectly relies on NX hardware enforcement before NXE is enabled, hardware will not enforce it. This stage covers that gap with source-level encoding checks and explicit documentation.

## User Address-Space Root Derivation

`uint64_t derive_user_address_space_root()`:

- Allocates one new PML4 page and accesses it through the direct map.
- **Copies the kernel PML4 high-half top-level entries** (indices 256..511, covering kernel higher half, self-mapping, direct map, and KVMEM) so kernel addresses are shared in the derived root.
- **Clears low-half entries** (indices 0..255), keeping user space independent.
- Returns the new root physical address, or `INVALID_PHYS_ADDR` on failure.

**Self-mapping semantics**: the derived root shares the kernel high half, so the self-mapping slot still comes from the kernel PML4. The first-user-program path relies only on high-half/direct-map/KVMEM reachability and the root-targeted mapper; it does not modify low-half page tables after the user root is active.

## Controlled Activation Boundary

`read_cr3()` / `activate_address_space_root(root)` are explicit APIs used only by `proc::run_user_process()`:

- The loader first builds low-half mappings with `derive_user_address_space_root()` and `map_page_in_root()`.
- Before entering ring3, it records the current kernel root, sets TSS/RSP0, then writes CR3 to activate the user root.
- `SYS_EXIT` or a controlled user fault records process state and restores the kernel root without immediately freeing the current stack/process object.
- Ordinary derivation helpers do not write CR3, enter ring3, or trigger demand paging.

## Validation

A default-off xmake option `user_vmem_smoke` defines `BIGOS_USER_VMEM_SMOKE`. When enabled, `kernel()` runs `bigos::mm::user_vmem_smoke()` once in non-interrupt context before IRQs are enabled. The smoke creates a `USER_DATA` mapping and reads the PTE to confirm user/writable/NX bits, confirms `USER_CODE` clears NX, derives a user root and checks high/low-half invariants, then unmaps/releases resources and emits deterministic markers:

- Success: `BIGOS_USER_VMEM_SMOKE_PASSED`
- Failure: `BIGOS_USER_VMEM_SMOKE_FAILED <stage>`

Source-level tests in `tests/test_user_address_space_vmem_source.py` pin explicit primitive attributes, kernel default supervisor `present+writable` equivalence, user/NX policy, derived-root high/low-half invariants, and the boundary that derivation does not implicitly write CR3 while the process runtime path explicitly activates the root.

If `tools/boot_debug.py` needs changes to inject this option and observe markers, that should be handled as a separate cross-cutting engineering item. Bochs runtime smoke depends on local ROMs, image locks, serial oracle, and interactive capability; when unavailable, validation should record why it was not run and the remaining bootability risk.
