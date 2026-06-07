# Address Space Lifecycle And Reclamation

This stage adds bounded reclamation for runtime-created page tables and the default-off first user program. It keeps boot/kernel/direct-map/KVMEM static mappings borrowed unless they are explicitly registered as dynamic owned page-table pages.

## Page-Table Ownership

`src/mm/vmem.cc` keeps a static `PageTableMetadata` table. The table records:

- Owner category: kernel vmem dynamic mappings or a derived user address space.
- Page-table level: PML4, PDPT, PD, or PT.
- Owning root physical frame and page-table frame.
- Present-entry count for reclaim decisions.

Metadata is registered before a present descriptor is published. If metadata registration, page-table allocation, direct-map access, or leaf publication fails, the current map operation rolls back its newly created descriptors and returns failure instead of leaving an untracked reclaimable page-table page.

Static or borrowed page tables are not entered in this registry. Reclaim paths treat missing metadata as non-reclaimable, which preserves boot handoff tables, kernel image mappings, copied high-half kernel entries, direct map, KVMEM static setup, and recursive self-mapping requirements.

## Empty-Table Reclaim

`unmap_page()` clears a present leaf PTE, executes same-CPU `invlpg`, decrements the PT present-entry count, and then checks PT, PD, and PDPT levels upward. A page-table frame is released only when:

- It has matching ownership metadata for the target root and owner.
- Its present-entry count is zero.
- The parent descriptor is cleared before returning the frame to the buddy allocator.

The public `alloc_kernel_pages(nr_pages, flags)` / `free_pages(ptr)` API remains page-count based. KVMEM freeing still unmaps before returning physical backing, and the empty-table reclaim is downstream of the unmap boundary.

## User Teardown

`teardown_user_address_space(root)` releases a derived user root only when it is not the active CR3 root. It traverses PML4 indices `0..255` only, so copied high-half kernel mappings remain borrowed and untouched.

For each user-owned low-half mapping, teardown clears the leaf PTE, returns the process-owned leaf physical page, releases empty PT/PD/PDPT frames, and releases the user PML4 root last. The root is inactive during teardown, so immediate `invlpg` is not required under the current single-core assumption; the root must not be reactivated after teardown begins.

## Process Reaper

`SYS_EXIT`, user-mode `#PF`, and invalid user buffers only mark the process terminated or faulted, record exit/fault information, restore the safe kernel root, and call the scheduler exit path. They do not reclaim the active syscall/fault stack or active user root on the same unsafe return path.

The scheduler idle loop calls `reap_pending_processes()` under `BIGOS_USER_PROGRAM_SMOKE`. The reaper runs in non-IRQ kernel context, checks that the target process kernel stack is not the current stack, rejects active-root teardown, then releases the user address space and kernel stack. Successful reaping emits `BIGOS_USER_RECLAIMED`; unsafe stack/root states emit deterministic defer markers.

## Validation

Source-level validation is in `tests/test_address_space_lifecycle_source.py`. It covers:

- Dynamic page-table metadata, publish ordering, rollback, and present-entry accounting.
- Empty PT/PD/PDPT reclaim and non-owned page-table rejection by metadata absence.
- User low-half-only teardown, high-half borrowed preservation, active-root rejection, and PML4-last release.
- `SYS_EXIT`, user `#PF`, and invalid user buffer handoff to the safe reaper.

Build and runtime validation for this change is recorded in `openspec/changes/reclaim-address-space-page-tables/validation.md`.
