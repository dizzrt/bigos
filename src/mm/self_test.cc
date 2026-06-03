#include <bigos/io.h>
#include <bigos/memory.h>

#include "buddy.h"
#include "memdef.h"
#include "slab.h"
#include "vmem.h"

namespace {
    constexpr const char *MM_SELF_TEST_PASSED = "BIGOS_MM_SELF_TEST_PASSED\n";
    constexpr const char *MM_SELF_TEST_FAILED = "BIGOS_MM_SELF_TEST_FAILED stage=";

    [[noreturn]] void fail(const char *__stage) noexcept {
        bigos::serial_puts(MM_SELF_TEST_FAILED);
        bigos::serial_puts(__stage);
        bigos::serial_puts("\n");
        bigos::kprintf("BIGOS_MM_SELF_TEST_FAILED stage=%s\n", __stage);

        while (true) {
            asm volatile("hlt");
        }
    }

    void touch_range(void *__ptr, uint32_t __size, uint8_t __seed) noexcept {
        volatile uint8_t *bytes = (volatile uint8_t *)__ptr;
        for (uint32_t i = 0; i < __size; i++)
            bytes[i] = (uint8_t)(__seed + i);
    }

    void touch_pages(void *__ptr, uint32_t __pages, uint8_t __seed) noexcept {
        volatile uint8_t *bytes = (volatile uint8_t *)__ptr;
        for (uint32_t page = 0; page < __pages; page++) {
            uint32_t base = page * PAGE_SIZE;
            bytes[base] = (uint8_t)(__seed + page);
            bytes[base + PAGE_SIZE - 1] = (uint8_t)(__seed + page + 1);
        }
    }

    void run_kmalloc_smoke() noexcept {
        constexpr uint32_t sizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
        void *objects[sizeof(sizes) / sizeof(sizes[0])] = {};

        for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            objects[i] = bigos::kmalloc(sizes[i]);
            if (objects[i] == nullptr)
                fail("kmalloc");
            touch_range(objects[i], sizes[i], (uint8_t)i);
        }

        for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
            bigos::free(objects[i]);
    }

    const bigos::mm::SlabCacheStats *find_cache(const bigos::mm::SlabAllocatorStats &__stats,
        uint32_t __object_size) noexcept {
        for (uint32_t i = 0; i < __stats.cache_count; i++) {
            if (__stats.caches[i].object_size == __object_size)
                return &__stats.caches[i];
        }
        return nullptr;
    }

    void run_large_kmalloc_smoke() noexcept {
        bigos::mm::SlabAllocatorStats before = {};
        bigos::mm::SlabAllocatorStats mid = {};
        bigos::mm::SlabAllocatorStats after = {};
        bigos::mm::collect_slab_stats(&before);

        constexpr uint32_t large_size = CACHE_MAX_OBJ_SIZE + 257;
        uint32_t vmem_before = bigos::mm::__detail::kernel_vmem_free_pages();

        void *object = bigos::kmalloc(large_size);
        if (object == nullptr)
            fail("kmalloc-large");

        touch_range(object, large_size, 0x40);
        bigos::mm::collect_slab_stats(&mid);
        if (mid.large_allocation_count != before.large_allocation_count + 1)
            fail("kmalloc-large-count");
        if (mid.large_allocation_pages <= before.large_allocation_pages)
            fail("kmalloc-large-pages");

        bigos::free(object);
        bigos::mm::collect_slab_stats(&after);
        if (after.large_allocation_count != before.large_allocation_count ||
            after.large_allocation_pages != before.large_allocation_pages ||
            after.large_allocation_bytes != before.large_allocation_bytes)
            fail("kmalloc-large-restore");
        if (bigos::mm::__detail::kernel_vmem_free_pages() != vmem_before)
            fail("kmalloc-large-vmem-restore");
    }

    void run_slab_reclaim_smoke() noexcept {
        constexpr uint32_t object_size = 64;
        constexpr uint32_t object_count = 256;
        void *objects[object_count] = {};

        bigos::mm::SlabAllocatorStats before = {};
        bigos::mm::SlabAllocatorStats after_alloc = {};
        bigos::mm::SlabAllocatorStats after_free = {};
        bigos::mm::collect_slab_stats(&before);

        for (uint32_t i = 0; i < object_count; i++) {
            objects[i] = bigos::kmalloc(object_size);
            if (objects[i] == nullptr)
                fail("slab-reclaim-alloc");
            touch_range(objects[i], object_size, (uint8_t)i);
        }

        bigos::mm::collect_slab_stats(&after_alloc);
        const auto *cache_after_alloc = find_cache(after_alloc, object_size);
        if (cache_after_alloc == nullptr || cache_after_alloc->slab_count < 2)
            fail("slab-reclaim-grow");

        for (uint32_t i = 0; i < object_count; i++)
            bigos::free(objects[i]);

        bigos::mm::collect_slab_stats(&after_free);
        const auto *cache_before = find_cache(before, object_size);
        const auto *cache_after_free = find_cache(after_free, object_size);
        if (cache_before == nullptr || cache_after_free == nullptr)
            fail("slab-reclaim-cache");
        if (cache_after_free->slab_count > cache_after_alloc->slab_count)
            fail("slab-reclaim-count");
        if (cache_after_free->reclaimed_slab_count <= cache_before->reclaimed_slab_count)
            fail("slab-reclaim-none");
    }

    void run_kernel_pages_smoke(uint32_t __pages, const char *__stage, bool __check_physical) noexcept {
        uint32_t physical_before = bigos::mm::g_nr_free_pages();
        uint32_t vmem_before = bigos::mm::__detail::kernel_vmem_free_pages();

        void *pages = bigos::alloc_kernel_pages(__pages, _GFM_PRE_PAGING);
        if (pages == nullptr)
            fail(__stage);

        touch_pages(pages, __pages, (uint8_t)__pages);
        bigos::free_pages(pages);

        if (bigos::mm::__detail::kernel_vmem_free_pages() != vmem_before)
            fail(__stage);

        if (__check_physical && bigos::mm::g_nr_free_pages() != physical_before)
            fail(__stage);
    }

    void run_physical_order_smoke(uint32_t __order) noexcept {
        uint32_t physical_before = bigos::mm::g_nr_free_pages();
        uint32_t pages = 1u << __order;

        void *physical = bigos::mm::__detail::alloc_physical_order(__order, 0);
        if (physical == nullptr)
            fail("physical-order");

        if (bigos::mm::g_nr_free_pages() != physical_before - pages)
            fail("physical-order-accounting");

        bigos::mm::__detail::free_physical_order(physical);
        if (bigos::mm::g_nr_free_pages() != physical_before)
            fail("physical-order-restore");
    }
}   // namespace

NAMESPACE_BIGOS_BEG
namespace mm {
    void self_test() noexcept {
        bigos::serial_init();

        run_kmalloc_smoke();
        run_large_kmalloc_smoke();
        run_slab_reclaim_smoke();

        // Prime retained page-table descriptors, then verify tested VMem ranges restore accounting.
        run_kernel_pages_smoke(513, "kernel-pages-prime", false);
        run_kernel_pages_smoke(1, "kernel-pages-1", true);
        run_kernel_pages_smoke(3, "kernel-pages-3", true);
        run_kernel_pages_smoke(513, "kernel-pages-513", true);

        run_physical_order_smoke(0);
        run_physical_order_smoke(1);
        run_physical_order_smoke(2);

        bigos::serial_puts(MM_SELF_TEST_PASSED);
        bigos::kputs(MM_SELF_TEST_PASSED);
    }
}   // namespace mm
NAMESPACE_BIGOS_END
