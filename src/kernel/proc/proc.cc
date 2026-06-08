#include <bigos/proc.h>

#include <string.h>
#include <bigos/io.h>
#include <bigos/memory.h>
#include <bigos/panic.h>
#include <bigos/sched.h>
#include <bigos/syscall.h>
#include <bigos/user_mode.h>

#include "../../mm/buddy.h"
#include "../../mm/memdef.h"
#include "../../mm/vmem.h"

namespace {
    constexpr uint32_t PROCESS_KERNEL_STACK_PAGES = 1;
    constexpr uint32_t ELF_MAX_LOAD_SEGMENTS = 8;
    constexpr uint32_t ELF_MAX_TRACKED_PAGES = 32;
    constexpr uint64_t USER_INITIAL_STACK_ALIGN = 16;
    constexpr uint64_t ELF_MAGIC = 0x464c457full;
    constexpr uint16_t ELF_TYPE_EXEC = 2;
    constexpr uint16_t ELF_MACHINE_X86_64 = 62;
    constexpr uint32_t ELF_VERSION_CURRENT = 1;
    constexpr uint32_t PT_LOAD = 1;
    constexpr uint32_t PT_DYNAMIC = 2;
    constexpr uint32_t PT_INTERP = 3;
    constexpr uint32_t PT_TLS = 7;
    constexpr uint32_t PF_X = 1;
    constexpr uint32_t PF_W = 2;
    constexpr uint32_t PF_R = 4;

    // Flat embedded image selected for stage 6: it avoids any kernel FS/block
    // dependency and still exercises code, data/BSS, stack, syscall write, exit.
    // The inline payload writes the deterministic BIGOS_USER_WRITE marker.
    constexpr uint8_t FIRST_USER_CODE[] = {
        0x48,
        0xc7,
        0xc0,
        0x02,
        0x00,
        0x00,
        0x00,   // mov $SYS_WRITE,%rax
        0x48,
        0xc7,
        0xc7,
        0x01,
        0x00,
        0x00,
        0x00,   // mov $1,%rdi
        0x48,
        0x8d,
        0x35,
        0x18,
        0x00,
        0x00,
        0x00,   // lea message(%rip),%rsi
        0x48,
        0xc7,
        0xc2,
        0x11,
        0x00,
        0x00,
        0x00,   // mov $17,%rdx
        0xcd,
        0x80,   // int $0x80
        0x48,
        0xc7,
        0xc0,
        0x03,
        0x00,
        0x00,
        0x00,   // mov $SYS_EXIT,%rax
        0x48,
        0x31,
        0xff,   // xor %rdi,%rdi
        0xcd,
        0x80,   // int $0x80
        0xf4,
        0xeb,
        0xfd,   // hlt; jmp .
        'B',
        'I',
        'G',
        'O',
        'S',
        '_',
        'U',
        'S',
        'E',
        'R',
        '_',
        'W',
        'R',
        'I',
        'T',
        'E',
        '\n',
    };

    bigos::proc::Process *g_current_process = nullptr;
    bigos::proc::Process *g_process_table[bigos::proc::MAX_PROCESSES] = {};
    uint32_t g_reap_head_pid = 0;
    uint32_t g_next_pid = 1;
    bool g_user_mode_initialized = false;
    bigos::sched::WaitQueue g_process_wait_queue = {};
    bool g_proc_initialized = false;

    uint64_t alloc_user_frame() noexcept {
        return (uint64_t)bigos::mm::__detail::alloc_physical_order(0, 0);
    }

    void free_user_frame(uint64_t __phys) noexcept {
        if (__phys != 0)
            bigos::mm::__detail::free_physical_order((void *)__phys);
    }

    bool zero_frame(uint64_t __phys) noexcept {
        void *direct = bigos::mm::phys_to_direct(__phys);
        if (direct == nullptr)
            return false;
        memset(direct, 0, PAGE_SIZE);
        return true;
    }

    bool copy_to_frame(uint64_t __phys, const void *__src, uint64_t __len) noexcept {
        void *direct = bigos::mm::phys_to_direct(__phys);
        if (direct == nullptr || __len > PAGE_SIZE)
            return false;
        memset(direct, 0, PAGE_SIZE);
        memcpy(direct, __src, __len);
        return true;
    }

    bool add_overflow(uint64_t __a, uint64_t __b, uint64_t *__out) noexcept {
        const uint64_t result = __a + __b;
        if (result < __a)
            return true;
        *__out = result;
        return false;
    }

    uint64_t page_down(uint64_t __value) noexcept {
        return __value & ~(PAGE_SIZE - 1);
    }

    bool page_up(uint64_t __value, uint64_t *__out) noexcept {
        uint64_t with_padding = 0;
        if (add_overflow(__value, PAGE_SIZE - 1, &with_padding))
            return false;
        *__out = page_down(with_padding);
        return true;
    }

    bool is_power_of_two(uint64_t __value) noexcept {
        return __value != 0 && (__value & (__value - 1)) == 0;
    }

    bool ranges_overlap(uint64_t __a_base, uint64_t __a_len, uint64_t __b_base, uint64_t __b_len) noexcept {
        uint64_t a_end = 0;
        uint64_t b_end = 0;
        if (add_overflow(__a_base, __a_len, &a_end) || add_overflow(__b_base, __b_len, &b_end))
            return true;
        return __a_base < b_end && __b_base < a_end;
    }

    bool user_range_valid(uint64_t __base, uint64_t __len) noexcept {
        if (__len == 0)
            return false;
        uint64_t end = 0;
        if (add_overflow(__base, __len, &end))
            return false;
        return end <= bigos::proc::USER_LOW_HALF_LIMIT;
    }

    bool page_aligned(uint64_t __value) noexcept {
        return (__value & (PAGE_SIZE - 1)) == 0;
    }

    uint8_t permissions_value(bigos::proc::VmaPermission __perm) noexcept {
        return (uint8_t)__perm;
    }

    bool permissions_include(bigos::proc::VmaPermission __have, bigos::proc::VmaPermission __need) noexcept {
        return (permissions_value(__have) & permissions_value(__need)) == permissions_value(__need);
    }

    bool permissions_wx(bigos::proc::VmaPermission __perm) noexcept {
        return permissions_include(__perm, bigos::proc::VmaPermission::Write) &&
               permissions_include(__perm, bigos::proc::VmaPermission::Execute);
    }

    bigos::proc::VmaPermission segment_permissions(uint32_t __flags) noexcept {
        uint8_t perm = permissions_value(bigos::proc::VmaPermission::Read);
        if ((__flags & PF_W) != 0)
            perm |= permissions_value(bigos::proc::VmaPermission::Write);
        if ((__flags & PF_X) != 0)
            perm |= permissions_value(bigos::proc::VmaPermission::Execute);
        return (bigos::proc::VmaPermission)perm;
    }

    bool vma_range_bounds(uint64_t __start, uint64_t __end) noexcept {
        return __start < __end && page_aligned(__start) && page_aligned(__end) &&
               __end <= bigos::proc::USER_LOW_HALF_LIMIT;
    }

    void internal_init_vmas(bigos::proc::VmaCollection *__vmas) noexcept {
        if (__vmas == nullptr)
            return;
        memset(__vmas, 0, sizeof(*__vmas));
        __vmas->anon_next = bigos::proc::USER_ANON_BASE;
    }

    bool internal_add_vma(bigos::proc::VmaCollection *__vmas, const bigos::proc::VmaEntry &__entry) noexcept {
        if (__vmas == nullptr || !vma_range_bounds(__entry.start, __entry.end) || permissions_wx(__entry.permissions) ||
            __vmas->count >= bigos::proc::MAX_VMAS)
            return false;

        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &existing = __vmas->entries[i];
            if (existing.used && __entry.start < existing.end && existing.start < __entry.end)
                return false;
        }

        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            if (!__vmas->entries[i].used) {
                __vmas->entries[i] = __entry;
                __vmas->entries[i].used = true;
                __vmas->count++;
                return true;
            }
        }
        return false;
    }

    bool internal_remove_vma(bigos::proc::VmaCollection *__vmas, uint64_t __start, uint64_t __end) noexcept {
        if (__vmas == nullptr)
            return false;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            bigos::proc::VmaEntry &entry = __vmas->entries[i];
            if (entry.used && entry.start == __start && entry.end == __end) {
                memset(&entry, 0, sizeof(entry));
                __vmas->count--;
                return true;
            }
        }
        return false;
    }

    const bigos::proc::VmaEntry *internal_find_vma(const bigos::proc::VmaCollection *__vmas, uint64_t __addr) noexcept {
        if (__vmas == nullptr || __addr >= bigos::proc::USER_LOW_HALF_LIMIT)
            return nullptr;
        for (uint32_t i = 0; i < bigos::proc::MAX_VMAS; i++) {
            const bigos::proc::VmaEntry &entry = __vmas->entries[i];
            if (entry.used && __addr >= entry.start && __addr < entry.end)
                return &entry;
        }
        return nullptr;
    }

    bigos::proc::VmaEntry *internal_find_vma_mut(bigos::proc::VmaCollection *__vmas, uint64_t __addr) noexcept {
        return (bigos::proc::VmaEntry *)internal_find_vma(__vmas, __addr);
    }

    bool internal_vma_range_allowed(const bigos::proc::VmaCollection *__vmas, uint64_t __addr, uint64_t __len,
        bigos::proc::VmaPermission __perm) noexcept {
        if (__len == 0 || !user_range_valid(__addr, __len))
            return false;

        uint64_t end = 0;
        if (add_overflow(__addr, __len, &end))
            return false;

        uint64_t cursor = __addr;
        while (cursor < end) {
            const bigos::proc::VmaEntry *entry = internal_find_vma(__vmas, cursor);
            if (entry == nullptr || !permissions_include(entry->permissions, __perm))
                return false;
            if (entry->end <= cursor)
                return false;
            cursor = entry->end < end ? entry->end : end;
        }
        return true;
    }

    bool internal_vma_attr_allowed(const bigos::proc::VmaEntry *__vma, bigos::mm::PageAttr __attr) noexcept {
        if (__vma == nullptr || (__attr & bigos::mm::page_attr::PRESENT) == 0 ||
            (__attr & bigos::mm::page_attr::USER) == 0)
            return false;
        if ((__attr & bigos::mm::page_attr::WRITABLE) != 0 &&
            !permissions_include(__vma->permissions, bigos::proc::VmaPermission::Write))
            return false;
        if ((__attr & bigos::mm::page_attr::NO_EXECUTE) == 0 &&
            !permissions_include(__vma->permissions, bigos::proc::VmaPermission::Execute))
            return false;
        return true;
    }

    bool map_user_page_for_process(bigos::proc::Process *__process, uint64_t __vaddr, uint64_t __phys,
        bigos::mm::PageAttr __attr, bool __also_map_active_root) noexcept {
        if (__process == nullptr || !page_aligned(__vaddr) || !page_aligned(__phys))
            return false;
        const bigos::proc::VmaEntry *vma = internal_find_vma(&__process->vmas, __vaddr);
        if (!internal_vma_range_allowed(&__process->vmas, __vaddr, PAGE_SIZE, bigos::proc::VmaPermission::Read) ||
            !internal_vma_attr_allowed(vma, __attr))
            return false;
        if (!bigos::mm::map_page_in_root(__process->address_space_root, __vaddr, __phys, __attr))
            return false;
        if (__also_map_active_root &&
            (bigos::mm::read_cr3() & 0x000ffffffffff000ull) != __process->address_space_root &&
            !bigos::mm::map_page(__vaddr, __phys, __attr))
            return false;
        return true;
    }

    void unmap_and_free_user_page(bigos::proc::Process *__process, uint64_t __vaddr) noexcept {
        if (__process == nullptr)
            return;
        uint64_t phys = bigos::mm::INVALID_PHYS_ADDR;
        if (bigos::mm::__detail::unmap_user_page_in_root(__process->address_space_root, __vaddr, &phys) &&
            phys != bigos::mm::INVALID_PHYS_ADDR)
            free_user_frame(phys);
    }

    bigos::proc::Process *lookup_process(uint32_t __pid) noexcept {
        if (__pid == 0)
            return nullptr;
        for (uint32_t i = 0; i < bigos::proc::MAX_PROCESSES; i++) {
            bigos::proc::Process *process = g_process_table[i];
            if (process != nullptr && process->pid == __pid)
                return process;
        }
        return nullptr;
    }

    uint32_t alloc_pid() noexcept {
        for (uint32_t attempt = 0; attempt < bigos::proc::MAX_PROCESSES * 2; attempt++) {
            const uint32_t pid = g_next_pid++;
            if (g_next_pid == 0 || g_next_pid == bigos::proc::WAIT_ANY)
                g_next_pid = 1;
            if (pid != 0 && pid != bigos::proc::WAIT_ANY && lookup_process(pid) == nullptr)
                return pid;
        }
        return 0;
    }

    bool publish_process(bigos::proc::Process *__process, uint32_t __parent_pid) noexcept {
        if (__process == nullptr || __process->table_published)
            return false;

        uint32_t slot = bigos::proc::MAX_PROCESSES;
        for (uint32_t i = 0; i < bigos::proc::MAX_PROCESSES; i++) {
            if (g_process_table[i] == nullptr) {
                slot = i;
                break;
            }
        }
        if (slot == bigos::proc::MAX_PROCESSES)
            return false;

        const uint32_t pid = alloc_pid();
        if (pid == 0)
            return false;

        __process->pid = pid;
        __process->parent_pid = __parent_pid;
        __process->first_child_pid = 0;
        __process->next_sibling_pid = 0;
        __process->next_reap_pid = 0;
        __process->table_published = true;
        __process->wait_status_consumed = false;
        __process->parent_waiting = false;
        g_process_table[slot] = __process;

        bigos::proc::Process *parent = lookup_process(__parent_pid);
        if (parent != nullptr) {
            __process->next_sibling_pid = parent->first_child_pid;
            parent->first_child_pid = pid;
        }
        return true;
    }

    void unpublish_process(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || !__process->table_published)
            return;

        for (uint32_t i = 0; i < bigos::proc::MAX_PROCESSES; i++) {
            if (g_process_table[i] == __process) {
                g_process_table[i] = nullptr;
                break;
            }
        }

        bigos::proc::Process *parent = lookup_process(__process->parent_pid);
        if (parent != nullptr) {
            uint32_t *link = &parent->first_child_pid;
            while (*link != 0) {
                bigos::proc::Process *child = lookup_process(*link);
                if (child == nullptr)
                    break;
                if (child == __process) {
                    *link = child->next_sibling_pid;
                    break;
                }
                link = &child->next_sibling_pid;
            }
        }

        __process->table_published = false;
        __process->pid = 0;
        __process->parent_pid = 0;
        __process->first_child_pid = 0;
        __process->next_sibling_pid = 0;
        __process->next_reap_pid = 0;
    }

    struct Elf64Header {
        uint8_t ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint64_t entry;
        uint64_t phoff;
        uint64_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    };

    struct Elf64ProgramHeader {
        uint32_t type;
        uint32_t flags;
        uint64_t offset;
        uint64_t vaddr;
        uint64_t paddr;
        uint64_t filesz;
        uint64_t memsz;
        uint64_t align;
    };

    struct LoadSegment {
        uint64_t vaddr;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t offset;
        uint64_t map_base;
        uint64_t map_len;
        uint32_t flags;
        bigos::mm::PageAttr attr;
    };

    struct TrackedPage {
        uint64_t vaddr;
        bigos::mm::PageAttr attr;
    };

    bigos::mm::PageAttr segment_attr(uint32_t __flags) noexcept {
        bigos::mm::PageAttr attr = bigos::mm::page_attr::PRESENT | bigos::mm::page_attr::USER;
        if ((__flags & PF_W) != 0)
            attr |= bigos::mm::page_attr::WRITABLE | bigos::mm::page_attr::NO_EXECUTE;
        else if ((__flags & PF_X) == 0)
            attr |= bigos::mm::page_attr::NO_EXECUTE;
        return attr;
    }

    bool is_writable_executable(uint32_t __flags) noexcept {
        return (__flags & PF_W) != 0 && (__flags & PF_X) != 0;
    }

    bool track_page(TrackedPage *__pages, uint32_t *__count, uint64_t __vaddr, bigos::mm::PageAttr __attr) noexcept {
        for (uint32_t i = 0; i < *__count; i++) {
            if (__pages[i].vaddr == __vaddr)
                return __pages[i].attr == __attr;
        }
        if (*__count >= ELF_MAX_TRACKED_PAGES)
            return false;
        __pages[*__count] = {__vaddr, __attr};
        (*__count)++;
        return true;
    }

    bigos::proc::UserElfLoadError validate_elf(const uint8_t *__image, uint64_t __image_len, LoadSegment *__segments,
        uint32_t *__segment_count, uint64_t *__entry) noexcept {
        if (__image == nullptr || __image_len < sizeof(Elf64Header) ||
            __image_len > bigos::proc::USER_ELF_MAX_FILE_BYTES)
            return bigos::proc::UserElfLoadError::InvalidArgument;

        const auto *ehdr = (const Elf64Header *)__image;
        if (*(const uint32_t *)ehdr->ident != ELF_MAGIC || ehdr->ident[4] != 2 || ehdr->ident[5] != 1 ||
            ehdr->ident[6] != ELF_VERSION_CURRENT)
            return bigos::proc::UserElfLoadError::UnsupportedElf;
        if (ehdr->type != ELF_TYPE_EXEC || ehdr->machine != ELF_MACHINE_X86_64 ||
            ehdr->version != ELF_VERSION_CURRENT || ehdr->ehsize != sizeof(Elf64Header) ||
            ehdr->phentsize != sizeof(Elf64ProgramHeader) || ehdr->phnum == 0 || ehdr->phnum > ELF_MAX_LOAD_SEGMENTS)
            return bigos::proc::UserElfLoadError::BadHeader;

        uint64_t ph_table_bytes = 0;
        uint64_t ph_table_end = 0;
        if (__builtin_mul_overflow((uint64_t)ehdr->phentsize, (uint64_t)ehdr->phnum, &ph_table_bytes) ||
            add_overflow(ehdr->phoff, ph_table_bytes, &ph_table_end) || ph_table_end > __image_len)
            return bigos::proc::UserElfLoadError::BadHeader;

        *__segment_count = 0;
        bool has_load = false;
        bool entry_executable = false;
        const uint64_t stack_base = bigos::proc::USER_STACK_TOP - bigos::proc::USER_STACK_PAGES * PAGE_SIZE;
        TrackedPage tracked_pages[ELF_MAX_TRACKED_PAGES] = {};
        uint32_t tracked_page_count = 0;

        for (uint16_t i = 0; i < ehdr->phnum; i++) {
            const uint64_t ph_offset = ehdr->phoff + (uint64_t)i * ehdr->phentsize;
            const auto *phdr = (const Elf64ProgramHeader *)(__image + ph_offset);
            if (phdr->type == PT_DYNAMIC || phdr->type == PT_INTERP || phdr->type == PT_TLS)
                return bigos::proc::UserElfLoadError::UnsupportedElf;
            if (phdr->type != PT_LOAD)
                return bigos::proc::UserElfLoadError::UnsupportedElf;
            if (phdr->memsz == 0 || phdr->filesz > phdr->memsz || is_writable_executable(phdr->flags))
                return bigos::proc::UserElfLoadError::UnsafePermissions;
            if (phdr->align != 0 && phdr->align != 1 && (!is_power_of_two(phdr->align) || phdr->align > PAGE_SIZE))
                return bigos::proc::UserElfLoadError::BadProgramHeader;

            uint64_t file_end = 0;
            uint64_t mem_end = 0;
            if (add_overflow(phdr->offset, phdr->filesz, &file_end) || file_end > __image_len ||
                add_overflow(phdr->vaddr, phdr->memsz, &mem_end) || !user_range_valid(phdr->vaddr, phdr->memsz) ||
                ranges_overlap(phdr->vaddr, phdr->memsz, stack_base, bigos::proc::USER_STACK_PAGES * PAGE_SIZE))
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            uint64_t map_end = 0;
            const uint64_t map_base = page_down(phdr->vaddr);
            if (!page_up(mem_end, &map_end) || map_end <= map_base)
                return bigos::proc::UserElfLoadError::AddressOutOfRange;

            for (uint32_t j = 0; j < *__segment_count; j++) {
                if (ranges_overlap(map_base, map_end - map_base, __segments[j].map_base, __segments[j].map_len))
                    return bigos::proc::UserElfLoadError::SegmentOverlap;
            }

            const bigos::mm::PageAttr attr = segment_attr(phdr->flags);
            for (uint64_t page = map_base; page < map_end; page += PAGE_SIZE) {
                if (!track_page(tracked_pages, &tracked_page_count, page, attr))
                    return bigos::proc::UserElfLoadError::UnsafePermissions;
            }

            LoadSegment &segment = __segments[*__segment_count];
            segment = {
                phdr->vaddr, phdr->memsz, phdr->filesz, phdr->offset, map_base, map_end - map_base, phdr->flags, attr};
            (*__segment_count)++;
            has_load = true;
            if ((phdr->flags & PF_X) != 0 && ehdr->entry >= phdr->vaddr && ehdr->entry < mem_end)
                entry_executable = true;
        }

        if (!has_load)
            return bigos::proc::UserElfLoadError::BadProgramHeader;
        if (!user_range_valid(ehdr->entry, 1))
            return bigos::proc::UserElfLoadError::AddressOutOfRange;
        if (!entry_executable)
            return bigos::proc::UserElfLoadError::EntryNotExecutable;
        *__entry = ehdr->entry;
        return bigos::proc::UserElfLoadError::Success;
    }

    bigos::proc::UserElfLoadError map_segment(
        bigos::proc::Process *__process, const uint8_t *__image, const LoadSegment &__segment) noexcept {
        for (uint64_t page = __segment.map_base; page < __segment.map_base + __segment.map_len; page += PAGE_SIZE) {
            const uint64_t phys = alloc_user_frame();
            bool mapped = false;
            if (phys == 0)
                return bigos::proc::UserElfLoadError::OutOfMemory;
            void *direct = bigos::mm::phys_to_direct(phys);
            if (direct == nullptr) {
                free_user_frame(phys);
                return bigos::proc::UserElfLoadError::CopyFailed;
            }
            memset(direct, 0, PAGE_SIZE);

            const uint64_t page_end = page + PAGE_SIZE;
            const uint64_t file_start = __segment.vaddr;
            const uint64_t file_end = __segment.vaddr + __segment.filesz;
            if (page < file_end && page_end > file_start) {
                const uint64_t copy_vaddr = page > file_start ? page : file_start;
                const uint64_t copy_end = page_end < file_end ? page_end : file_end;
                const uint64_t page_offset = copy_vaddr - page;
                const uint64_t file_offset = __segment.offset + (copy_vaddr - __segment.vaddr);
                memcpy((uint8_t *)direct + page_offset, __image + file_offset, copy_end - copy_vaddr);
            }

            if (!map_user_page_for_process(__process, page, phys, __segment.attr, true))
                goto fail;
            mapped = true;
            (void)mapped;
            continue;

        fail:
            if (!mapped)
                free_user_frame(phys);
            return bigos::proc::UserElfLoadError::MapFailed;
        }
        return bigos::proc::UserElfLoadError::Success;
    }

    [[noreturn]] void halt_failed(const char *__marker) noexcept {
        bigos::serial_puts(__marker);
        bigos::khalt();
    }

    uint64_t current_stack_pointer() noexcept {
        uint64_t rsp;
        asm volatile("movq %%rsp, %0" : "=r"(rsp));
        return rsp;
    }

    bool stack_is_active(const bigos::proc::Process *process) noexcept {
        if (process == nullptr || process->kernel_stack_base == nullptr)
            return false;
        const uint64_t rsp = current_stack_pointer();
        const uint64_t base = (uint64_t)process->kernel_stack_base;
        return rsp >= base && rsp < base + process->kernel_stack_len;
    }

    void mark_reap_pending(bigos::proc::Process *process) noexcept {
        if (process == nullptr || process->reap_pending)
            return;
        process->reap_pending = true;
        process->state = bigos::proc::ProcessState::ReapPending;
        process->next_reap_pid = g_reap_head_pid;
        g_reap_head_pid = process->pid;
    }

    void mark_zombie_or_reap_pending(bigos::proc::Process *process) noexcept {
        if (process == nullptr)
            return;

        bigos::proc::Process *parent = lookup_process(process->parent_pid);
        if (parent != nullptr && !process->wait_status_consumed) {
            process->state = bigos::proc::ProcessState::Zombie;
            (void)bigos::sched::wake_all(&g_process_wait_queue);
            return;
        }

        process->wait_status_consumed = true;
        mark_reap_pending(process);
    }

    bool clone_process_kernel_stack_mapping(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr || __process->kernel_stack_base == nullptr ||
            __process->address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            return false;

        const uint64_t base = (uint64_t)__process->kernel_stack_base;
        for (uint32_t i = 0; i < PROCESS_KERNEL_STACK_PAGES; i++) {
            if (!bigos::mm::__detail::clone_kernel_page_mapping_in_root(
                    __process->address_space_root, base + (uint64_t)i * PAGE_SIZE))
                return false;
        }
        return true;
    }

    void init_fd_table(bigos::proc::Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < bigos::proc::MAX_FDS; i++) {
            __process->fd_table[i].file = nullptr;
            __process->fd_table[i].close_on_exec = false;
            __process->fd_table[i].readable = false;
        }
    }

    uint64_t bounded_strlen(const char *__value, uint64_t __limit) noexcept {
        if (__value == nullptr)
            return UINT64_MAX;
        uint64_t len = 0;
        while (len <= __limit) {
            if (__value[len] == 0)
                return len;
            len++;
        }
        return UINT64_MAX;
    }

    bool copy_exec_args_to_stack(uint64_t __stack_phys, uint64_t __stack_base, const bigos::proc::ExecArgs *__args,
        uint64_t *__initial_sp) noexcept {
        if (__initial_sp == nullptr)
            return false;
        uint8_t *direct = (uint8_t *)bigos::mm::phys_to_direct(__stack_phys);
        if (direct == nullptr)
            return false;

        const char *empty_argv[] = {nullptr};
        bigos::proc::ExecArgs empty_args = {empty_argv, 0, nullptr, 0};
        const bigos::proc::ExecArgs *args = __args == nullptr ? &empty_args : __args;
        if (args->argc > bigos::proc::EXEC_MAX_ARGC || args->envc > bigos::proc::EXEC_MAX_ENVC)
            return false;
        if ((args->argc != 0 && args->argv == nullptr) || (args->envc != 0 && args->envp == nullptr))
            return false;

        uint64_t sp = PAGE_SIZE;
        uint64_t argv_user[bigos::proc::EXEC_MAX_ARGC] = {};
        uint64_t envp_user[bigos::proc::EXEC_MAX_ENVC] = {};

        for (uint32_t i = 0; i < args->envc; i++) {
            const uint64_t len = bounded_strlen(args->envp[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->envp[i], len + 1);
            envp_user[i] = __stack_base + sp;
        }
        for (uint32_t i = 0; i < args->argc; i++) {
            const uint64_t len = bounded_strlen(args->argv[i], bigos::proc::EXEC_MAX_STRING_BYTES);
            if (len == UINT64_MAX || len + 1 > sp)
                return false;
            sp -= len + 1;
            memcpy(direct + sp, args->argv[i], len + 1);
            argv_user[i] = __stack_base + sp;
        }

        sp &= ~(USER_INITIAL_STACK_ALIGN - 1);
        const uint64_t envp_bytes = (uint64_t)(args->envc + 1) * sizeof(uint64_t);
        const uint64_t argv_bytes = (uint64_t)(args->argc + 1) * sizeof(uint64_t);
        const uint64_t frame_bytes = sizeof(uint64_t) + argv_bytes + envp_bytes;
        if (frame_bytes > sp)
            return false;

        sp -= envp_bytes;
        uint64_t *envp_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->envc; i++)
            envp_table[i] = envp_user[i];
        envp_table[args->envc] = 0;

        sp -= argv_bytes;
        uint64_t *argv_table = (uint64_t *)(direct + sp);
        for (uint32_t i = 0; i < args->argc; i++)
            argv_table[i] = argv_user[i];
        argv_table[args->argc] = 0;

        sp -= sizeof(uint64_t);
        *(uint64_t *)(direct + sp) = args->argc;
        *__initial_sp = __stack_base + sp;
        return true;
    }
}   // namespace

namespace bigos::proc {
    void init() noexcept {
        if (g_proc_initialized)
            return;
        for (uint32_t i = 0; i < MAX_PROCESSES; i++)
            g_process_table[i] = nullptr;
        g_current_process = nullptr;
        g_reap_head_pid = 0;
        g_next_pid = 1;
        bigos::sched::init_wait_queue(&g_process_wait_queue);
        g_proc_initialized = true;
        bigos::serial_puts("BIGOS_PROC_INIT\n");
    }

    bool create_first_user_process(Process *__process) noexcept {
        if (__process == nullptr || sizeof(FIRST_USER_CODE) > PAGE_SIZE)
            return false;

        if (!g_proc_initialized)
            init();
        memset(__process, 0, sizeof(*__process));
        uint64_t code_phys = 0;
        uint64_t data_phys = 0;
        uint64_t stack_phys = 0;
        bool code_mapped = false;
        bool data_mapped = false;
        bool stack_mapped = false;
        const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;

        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr)
            return false;
        __process->address_space_root = bigos::mm::derive_user_address_space_root();
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (__process->address_space_root == bigos::mm::INVALID_PHYS_ADDR)
            goto fail;
        if (!clone_process_kernel_stack_mapping(__process))
            goto fail;
        internal_init_vmas(&__process->vmas);
        if (!internal_add_vma(&__process->vmas,
                {USER_CODE_BASE, USER_CODE_BASE + PAGE_SIZE, USER_CODE_BASE, USER_CODE_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Execute)),
                    VmaPurpose::Code, VmaBacking::Anonymous, VmaGrowth::None, true}) ||
            !internal_add_vma(&__process->vmas,
                {USER_DATA_BASE, USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE, USER_DATA_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Data, VmaBacking::Anonymous, VmaGrowth::None, true}) ||
            !internal_add_vma(&__process->vmas,
                {USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE + PAGE_SIZE + USER_HEAP_MAX_PAGES * PAGE_SIZE,
                    USER_DATA_BASE + PAGE_SIZE, USER_DATA_BASE + PAGE_SIZE,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Heap, VmaBacking::Anonymous, VmaGrowth::Up, true}) ||
            !internal_add_vma(&__process->vmas,
                {stack_base - (USER_STACK_GROWTH_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE,
                    stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, stack_base, stack_base, VmaPermission::None,
                    VmaPurpose::StackGuard, VmaBacking::Guard, VmaGrowth::None, true}) ||
            !internal_add_vma(&__process->vmas,
                {stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, USER_STACK_TOP, stack_base, USER_STACK_TOP,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Stack, VmaBacking::Anonymous, VmaGrowth::Down, true}))
            goto fail;
        __process->vmas.heap_base = USER_DATA_BASE + PAGE_SIZE;
        __process->vmas.heap_break = __process->vmas.heap_base;
        __process->vmas.heap_limit = __process->vmas.heap_base + USER_HEAP_MAX_PAGES * PAGE_SIZE;

        code_phys = alloc_user_frame();
        data_phys = alloc_user_frame();
        stack_phys = alloc_user_frame();
        if (code_phys == 0 || data_phys == 0 || stack_phys == 0)
            goto fail;
        if (!copy_to_frame(code_phys, FIRST_USER_CODE, sizeof(FIRST_USER_CODE)) || !zero_frame(data_phys) ||
            !zero_frame(stack_phys))
            goto fail;

        if (!map_user_page_for_process(__process, USER_CODE_BASE, code_phys, bigos::mm::page_attr::USER_CODE, true))
            goto fail;
        code_mapped = true;
        if (!map_user_page_for_process(__process, USER_DATA_BASE, data_phys, bigos::mm::page_attr::USER_DATA, true))
            goto fail;
        data_mapped = true;
        if (!map_user_page_for_process(__process, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA, true))
            goto fail;
        stack_mapped = true;

        __process->code_phys = code_phys;
        __process->data_phys = data_phys;
        __process->stack_phys = stack_phys;
        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top = (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = USER_CODE_BASE;
        __process->code = {USER_CODE_BASE, PAGE_SIZE};
        __process->data = {USER_DATA_BASE, PAGE_SIZE};
        __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
        __process->initial_stack = USER_STACK_TOP;
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        init_fd_table(__process);
        if (!publish_process(__process, ROOT_PARENT_PID))
            goto fail;
        return true;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        if (!code_mapped)
            free_user_frame(code_phys);
        if (!data_mapped)
            free_user_frame(data_phys);
        if (!stack_mapped)
            free_user_frame(stack_phys);
        if (__process->table_published)
            unpublish_process(__process);
        memset(__process, 0, sizeof(*__process));
        __process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        return false;
    }

    const char *user_elf_load_error_name(UserElfLoadError __error) noexcept {
        switch (__error) {
            case UserElfLoadError::Success:
                return "success";
            case UserElfLoadError::InvalidArgument:
                return "invalid-argument";
            case UserElfLoadError::UnsupportedElf:
                return "unsupported-elf";
            case UserElfLoadError::BadHeader:
                return "bad-header";
            case UserElfLoadError::BadProgramHeader:
                return "bad-program-header";
            case UserElfLoadError::AddressOutOfRange:
                return "address-out-of-range";
            case UserElfLoadError::SegmentOverlap:
                return "segment-overlap";
            case UserElfLoadError::UnsafePermissions:
                return "unsafe-permissions";
            case UserElfLoadError::EntryNotExecutable:
                return "entry-not-executable";
            case UserElfLoadError::OutOfMemory:
                return "out-of-memory";
            case UserElfLoadError::MapFailed:
                return "map-failed";
            case UserElfLoadError::CopyFailed:
                return "copy-failed";
        }
        return "unknown";
    }

    UserElfLoadError create_elf_user_process(
        Process *__process, const void *__image, uint64_t __image_len, const ExecArgs *__args) noexcept {
        if (__process == nullptr)
            return UserElfLoadError::InvalidArgument;
        if (!g_proc_initialized)
            init();
        LoadSegment segments[ELF_MAX_LOAD_SEGMENTS] = {};
        uint32_t segment_count = 0;
        uint64_t entry = 0;
        uint64_t image_high = 0;
        uint64_t heap_base = 0;
        UserElfLoadError error = validate_elf((const uint8_t *)__image, __image_len, segments, &segment_count, &entry);
        if (error != UserElfLoadError::Success)
            return error;

        memset(__process, 0, sizeof(*__process));
        __process->kernel_stack_base = bigos::alloc_kernel_pages(PROCESS_KERNEL_STACK_PAGES, _GFM_PRE_PAGING);
        if (__process->kernel_stack_base == nullptr)
            return UserElfLoadError::OutOfMemory;
        __process->address_space_root = bigos::mm::derive_user_address_space_root();
        __process->kernel_address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        if (__process->address_space_root == bigos::mm::INVALID_PHYS_ADDR) {
            error = UserElfLoadError::OutOfMemory;
            goto fail;
        }
        if (!clone_process_kernel_stack_mapping(__process)) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }
        internal_init_vmas(&__process->vmas);
        for (uint32_t i = 0; i < segment_count; i++) {
            const VmaPurpose purpose = (segments[i].flags & PF_X) != 0 ? VmaPurpose::Code : VmaPurpose::Data;
            if (!internal_add_vma(&__process->vmas,
                    {segments[i].map_base, segments[i].map_base + segments[i].map_len, segments[i].map_base,
                        segments[i].map_base + segments[i].map_len, segment_permissions(segments[i].flags), purpose,
                        VmaBacking::ElfSegment, VmaGrowth::None, true})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            if (segments[i].map_base + segments[i].map_len > image_high)
                image_high = segments[i].map_base + segments[i].map_len;
        }
        if (!page_up(image_high, &heap_base)) {
            error = UserElfLoadError::AddressOutOfRange;
            goto fail;
        }
        __process->vmas.heap_base = heap_base;
        __process->vmas.heap_break = heap_base;
        __process->vmas.heap_limit = heap_base + USER_HEAP_MAX_PAGES * PAGE_SIZE;
        if (!internal_add_vma(&__process->vmas,
                {heap_base, __process->vmas.heap_limit, heap_base, heap_base,
                    (VmaPermission)(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write)),
                    VmaPurpose::Heap, VmaBacking::Anonymous, VmaGrowth::Up, true})) {
            error = UserElfLoadError::MapFailed;
            goto fail;
        }

        for (uint32_t i = 0; i < segment_count; i++) {
            error = map_segment(__process, (const uint8_t *)__image, segments[i]);
            if (error != UserElfLoadError::Success)
                goto fail;
        }

        {
            const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
            const uint64_t stack_phys = alloc_user_frame();
            uint64_t initial_sp = 0;
            if (!internal_add_vma(&__process->vmas,
                    {stack_base - (USER_STACK_GROWTH_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE,
                        stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, stack_base, stack_base, VmaPermission::None,
                        VmaPurpose::StackGuard, VmaBacking::Guard, VmaGrowth::None, true}) ||
                !internal_add_vma(&__process->vmas,
                    {stack_base - USER_STACK_GROWTH_PAGES * PAGE_SIZE, USER_STACK_TOP, stack_base, USER_STACK_TOP,
                        (VmaPermission)(permissions_value(VmaPermission::Read) |
                                        permissions_value(VmaPermission::Write)),
                        VmaPurpose::Stack, VmaBacking::Anonymous, VmaGrowth::Down, true})) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            if (stack_phys == 0) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!zero_frame(stack_phys)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!copy_exec_args_to_stack(stack_phys, stack_base, __args, &initial_sp)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!map_user_page_for_process(__process, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA, true)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            __process->stack_phys = stack_phys;
            __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
            __process->initial_stack = initial_sp;
        }

        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top = (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = entry;
        __process->code = {segments[0].map_base, segments[0].map_len};
        __process->data = {0, 0};
        for (uint32_t i = 0; i < segment_count; i++) {
            if ((segments[i].flags & PF_X) != 0)
                __process->code = {segments[i].map_base, segments[i].map_len};
            if ((segments[i].flags & PF_W) != 0 && __process->data.len == 0)
                __process->data = {segments[i].map_base, segments[i].map_len};
        }
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
        init_fd_table(__process);
        if (!publish_process(__process, g_current_process != nullptr ? g_current_process->pid : ROOT_PARENT_PID)) {
            error = UserElfLoadError::OutOfMemory;
            goto fail;
        }
        return UserElfLoadError::Success;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        if (__process->table_published)
            unpublish_process(__process);
        memset(__process, 0, sizeof(*__process));
        __process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        return error;
    }

    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len) noexcept {
        return create_elf_user_process(__process, __image, __image_len, nullptr);
    }

    UserElfLoadError exec_current_from_elf_image(
        const void *__image, uint64_t __image_len, const ExecArgs *__args) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running)
            return UserElfLoadError::InvalidArgument;
        if (process->address_space_root == (bigos::mm::read_cr3() & 0x000ffffffffff000ull))
            return UserElfLoadError::MapFailed;

        Process prepared = {};
        UserElfLoadError error = create_elf_user_process(&prepared, __image, __image_len, __args);
        if (error != UserElfLoadError::Success)
            return error;

        unpublish_process(&prepared);
        const uint64_t old_root = process->address_space_root;
        const UserRange old_code = process->code;
        const UserRange old_data = process->data;
        const UserRange old_stack = process->stack;
        const VmaCollection old_vmas = process->vmas;
        const uint64_t old_code_phys = process->code_phys;
        const uint64_t old_data_phys = process->data_phys;
        const uint64_t old_stack_phys = process->stack_phys;

        process->address_space_root = prepared.address_space_root;
        process->entry = prepared.entry;
        process->code = prepared.code;
        process->data = prepared.data;
        process->stack = prepared.stack;
        process->vmas = prepared.vmas;
        process->initial_stack = prepared.initial_stack;
        process->code_phys = prepared.code_phys;
        process->data_phys = prepared.data_phys;
        process->stack_phys = prepared.stack_phys;

        if (prepared.kernel_stack_base != nullptr)
            bigos::free_pages(prepared.kernel_stack_base);
        prepared.address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        prepared.kernel_stack_base = nullptr;

        if (old_root != bigos::mm::INVALID_PHYS_ADDR && !bigos::mm::teardown_user_address_space(old_root)) {
            process->exit_code = EXEC_FAILURE_STATUS;
            process->fault_reason = EXEC_FAILURE_STATUS;
            mark_zombie_or_reap_pending(process);
            return UserElfLoadError::MapFailed;
        }
        close_on_exec_fds(process);
        (void)old_code;
        (void)old_data;
        (void)old_stack;
        (void)old_vmas;
        (void)old_code_phys;
        (void)old_data_phys;
        (void)old_stack_phys;
        return UserElfLoadError::Success;
    }

    [[noreturn]] void run_user_process(Process *__process) noexcept {
        if (__process == nullptr || __process->state != ProcessState::Created)
            halt_failed("BIGOS_USER_LOAD_FAILED invalid-process\n");

        if (!g_user_mode_initialized) {
            bigos::arch::x86::init_user_mode();
            g_user_mode_initialized = true;
        }

        g_current_process = __process;
        __process->state = ProcessState::Running;
        __process->kernel_address_space_root = bigos::mm::read_cr3();
        bigos::arch::x86::set_tss_rsp0(__process->kernel_stack_top);
        bigos::serial_puts("BIGOS_USER_ENTER\n");
        const uint64_t stack =
            __process->initial_stack != 0 ? __process->initial_stack : __process->stack.base + __process->stack.len;
        bigos::mm::activate_address_space_root(__process->address_space_root);
        bigos::arch::x86::enter_user_mode(__process->entry, stack);
    }

    Process *current_process() noexcept {
        return g_current_process;
    }

    void init_vmas(VmaCollection *__vmas) noexcept {
        internal_init_vmas(__vmas);
    }

    bool add_vma(VmaCollection *__vmas, const VmaEntry &__entry) noexcept {
        return internal_add_vma(__vmas, __entry);
    }

    bool remove_vma(VmaCollection *__vmas, uint64_t __start, uint64_t __end) noexcept {
        return internal_remove_vma(__vmas, __start, __end);
    }

    const VmaEntry *find_vma(const VmaCollection *__vmas, uint64_t __addr) noexcept {
        return internal_find_vma(__vmas, __addr);
    }

    bool vma_range_allowed(
        const VmaCollection *__vmas, uint64_t __addr, uint64_t __len, VmaPermission __perm) noexcept {
        return internal_vma_range_allowed(__vmas, __addr, __len, __perm);
    }

    bool vma_attr_allowed(const VmaEntry *__vma, bigos::mm::PageAttr __attr) noexcept {
        return internal_vma_attr_allowed(__vma, __attr);
    }

    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_WRITE_MAX_LEN)
            return false;
        return internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Read) &&
               bigos::mm::user_range_mapped(process->address_space_root, __addr, __len);
    }

    bool validate_user_io_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        return internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Write) &&
               bigos::mm::__detail::user_range_writable(process->address_space_root, __addr, __len);
    }

    bool copy_current_user_buffer(uint64_t __addr, void *__dst, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_WRITE_MAX_LEN)
            return false;
        if (!internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Read))
            return false;
        return bigos::mm::__detail::copy_from_user_root(process->address_space_root, __addr, __dst, __len);
    }

    bool copy_to_current_user_buffer(uint64_t __addr, const void *__src, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_IO_MAX_LEN)
            return false;
        if (!internal_vma_range_allowed(&process->vmas, __addr, __len, VmaPermission::Write))
            return false;
        return bigos::mm::__detail::copy_to_user_root(process->address_space_root, __addr, __src, __len);
    }

    int64_t brk_current(uint64_t __new_break) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return bigos::sys::SYS_EWOULDBLOCK;
        if (__new_break == 0)
            return (int64_t)process->vmas.heap_break;
        if (__new_break < process->vmas.heap_base || __new_break > process->vmas.heap_limit)
            return bigos::sys::SYS_EINVAL;

        VmaEntry *heap = internal_find_vma_mut(&process->vmas, process->vmas.heap_base);
        if (heap == nullptr || heap->purpose != VmaPurpose::Heap)
            return bigos::sys::SYS_EFAULT;

        const uint64_t old_break = process->vmas.heap_break;
        uint64_t old_page_end = 0;
        uint64_t new_page_end = 0;
        if (!page_up(old_break, &old_page_end) || !page_up(__new_break, &new_page_end))
            return bigos::sys::SYS_EINVAL;

        if (new_page_end > old_page_end) {
            uint64_t mapped_phys[USER_HEAP_MAX_PAGES] = {};
            uint32_t mapped_count = 0;
            for (uint64_t page = old_page_end; page < new_page_end; page += PAGE_SIZE) {
                const uint64_t phys = alloc_user_frame();
                if (phys == 0 || !zero_frame(phys) ||
                    !map_user_page_for_process(process, page, phys, bigos::mm::page_attr::USER_DATA, false)) {
                    free_user_frame(phys);
                    for (uint32_t i = 0; i < mapped_count; i++)
                        unmap_and_free_user_page(process, old_page_end + (uint64_t)i * PAGE_SIZE);
                    return bigos::sys::SYS_EFAULT;
                }
                mapped_phys[mapped_count++] = phys;
            }
            (void)mapped_phys;
        } else if (new_page_end < old_page_end) {
            for (uint64_t page = new_page_end; page < old_page_end; page += PAGE_SIZE)
                unmap_and_free_user_page(process, page);
        }

        process->vmas.heap_break = __new_break;
        heap->materialized_end = new_page_end;
        return (int64_t)process->vmas.heap_break;
    }

    int64_t map_anonymous_current(uint64_t __len, uint64_t __permissions, uint64_t __flags) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return bigos::sys::SYS_EWOULDBLOCK;
        if (__flags != 0 || __len == 0 || __len > USER_ANON_MAX_PAGES * PAGE_SIZE)
            return bigos::sys::SYS_EINVAL;
        uint64_t len = 0;
        if (!page_up(__len, &len))
            return bigos::sys::SYS_EINVAL;

        const auto permissions = (VmaPermission)(uint8_t)__permissions;
        if ((permissions_value(permissions) &
                ~(permissions_value(VmaPermission::Read) | permissions_value(VmaPermission::Write) |
                    permissions_value(VmaPermission::Execute))) != 0 ||
            permissions_wx(permissions))
            return bigos::sys::SYS_EINVAL;

        uint64_t base = process->vmas.anon_next;
        uint64_t end = 0;
        while (!add_overflow(base, len, &end) && end <= USER_ANON_BASE + USER_ANON_MAX_PAGES * PAGE_SIZE) {
            bool overlaps = false;
            for (uint32_t i = 0; i < MAX_VMAS; i++) {
                const VmaEntry &entry = process->vmas.entries[i];
                if (entry.used && base < entry.end && entry.start < end)
                    overlaps = true;
            }
            if (!overlaps)
                break;
            base += PAGE_SIZE;
        }
        if (add_overflow(base, len, &end) || end > USER_ANON_BASE + USER_ANON_MAX_PAGES * PAGE_SIZE)
            return bigos::sys::SYS_EINVAL;

        if (!internal_add_vma(&process->vmas, {base, end, base, base, permissions, VmaPurpose::Anonymous,
                                                  VmaBacking::Anonymous, VmaGrowth::None, true}))
            return bigos::sys::SYS_EFAULT;

        uint64_t mapped_pages = 0;
        bigos::mm::PageAttr attr = bigos::mm::page_attr::PRESENT | bigos::mm::page_attr::USER;
        if (permissions_include(permissions, VmaPermission::Write))
            attr |= bigos::mm::page_attr::WRITABLE | bigos::mm::page_attr::NO_EXECUTE;
        else if (!permissions_include(permissions, VmaPermission::Execute))
            attr |= bigos::mm::page_attr::NO_EXECUTE;

        for (uint64_t page = base; page < end; page += PAGE_SIZE) {
            const uint64_t phys = alloc_user_frame();
            if (phys == 0 || !zero_frame(phys) || !map_user_page_for_process(process, page, phys, attr, false)) {
                free_user_frame(phys);
                for (uint64_t rollback = base; rollback < base + mapped_pages * PAGE_SIZE; rollback += PAGE_SIZE)
                    unmap_and_free_user_page(process, rollback);
                (void)internal_remove_vma(&process->vmas, base, end);
                return bigos::sys::SYS_EFAULT;
            }
            mapped_pages++;
        }

        VmaEntry *anon = internal_find_vma_mut(&process->vmas, base);
        if (anon != nullptr)
            anon->materialized_end = end;
        process->vmas.anon_next = end;
        return (int64_t)base;
    }

    bool try_handle_current_stack_fault(uint64_t __fault_address, uint64_t __error_code) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || !bigos::sched::can_block())
            return false;
        if ((__error_code & 0x1) != 0 || (__error_code & (1ull << 4)) != 0)
            return false;

        const uint64_t page = page_down(__fault_address);
        VmaEntry *stack = internal_find_vma_mut(&process->vmas, page);
        if (stack == nullptr || stack->purpose != VmaPurpose::Stack || stack->growth != VmaGrowth::Down ||
            page < stack->start || page >= stack->materialized_start)
            return false;
        if ((__error_code & 0x2) != 0 && !permissions_include(stack->permissions, VmaPermission::Write))
            return false;

        const uint64_t phys = alloc_user_frame();
        if (phys == 0)
            return false;
        if (!zero_frame(phys) ||
            !map_user_page_for_process(process, page, phys, bigos::mm::page_attr::USER_DATA, false)) {
            free_user_frame(phys);
            return false;
        }
        stack->materialized_start = page;
        return true;
    }

    int64_t install_fd_current(bigos::vfs::File *__file, bool __close_on_exec) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __file == nullptr)
            return FD_EBADF;
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            if (process->fd_table[i].file == nullptr) {
                process->fd_table[i].file = __file;
                process->fd_table[i].close_on_exec = __close_on_exec;
                process->fd_table[i].readable = __file->readable;
                return (int64_t)i;
            }
        }
        return FD_EMFILE;
    }

    bigos::vfs::Status read_fd_current(uint32_t __fd, void *__dst, size_t __len, size_t *__bytes_read) noexcept {
        if (__bytes_read != nullptr)
            *__bytes_read = 0;
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __fd >= MAX_FDS)
            return bigos::vfs::Status::BadFileDescriptor;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr || !entry->readable)
            return bigos::vfs::Status::BadFileDescriptor;
        return bigos::vfs::read(entry->file, __dst, __len, __bytes_read);
    }

    int64_t close_fd_current(uint32_t __fd) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __fd >= MAX_FDS)
            return FD_EBADF;
        FdEntry *entry = &process->fd_table[__fd];
        if (entry->file == nullptr)
            return FD_EBADF;
        bigos::vfs::File *file = entry->file;
        entry->file = nullptr;
        entry->close_on_exec = false;
        entry->readable = false;
        bigos::vfs::release(file);
        return 0;
    }

    void close_all_fds(Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            FdEntry *entry = &__process->fd_table[i];
            if (entry->file == nullptr)
                continue;
            bigos::vfs::File *file = entry->file;
            entry->file = nullptr;
            entry->close_on_exec = false;
            entry->readable = false;
            bigos::vfs::release(file);
        }
    }

    void close_on_exec_fds(Process *__process) noexcept {
        if (__process == nullptr)
            return;
        for (uint32_t i = 0; i < MAX_FDS; i++) {
            FdEntry *entry = &__process->fd_table[i];
            if (entry->file == nullptr || !entry->close_on_exec)
                continue;
            bigos::vfs::File *file = entry->file;
            entry->file = nullptr;
            entry->close_on_exec = false;
            entry->readable = false;
            bigos::vfs::release(file);
        }
    }

    void mark_current_faulted(int64_t __reason) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            return;
        process->state = ProcessState::Faulted;
        process->exit_code = __reason;
        process->fault_reason = __reason;
        mark_zombie_or_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_PAGE_FAULT\n");
        if (process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::mm::activate_address_space_root(process->kernel_address_space_root);
    }

    [[noreturn]] void fault_current_and_exit(int64_t __reason) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            halt_failed("BIGOS_USER_FAULT_FAILED no-process\n");

        mark_current_faulted(__reason);
        g_current_process = nullptr;
        bigos::sched::thread_exit();
    }

    [[noreturn]] void exit_current(int64_t __code) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            halt_failed("BIGOS_USER_EXIT_FAILED no-process\n");

        process->state = ProcessState::Terminated;
        process->exit_code = __code;
        mark_zombie_or_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_EXIT\n");
        if (process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::mm::activate_address_space_root(process->kernel_address_space_root);
        g_current_process = nullptr;
        bigos::sched::thread_exit();
    }

    int64_t wait_current(uint32_t __pid, int64_t *__status) noexcept {
        Process *parent = g_current_process;
        if (parent == nullptr)
            return WAIT_ECHILD;
        if (!bigos::sched::can_block())
            return WAIT_EWOULDBLOCK;

        Process *match = nullptr;
        for (;;) {
            uint32_t child_pid = parent->first_child_pid;
            bool has_matching_child = false;
            while (child_pid != 0) {
                Process *child = lookup_process(child_pid);
                if (child == nullptr)
                    break;
                if (__pid == WAIT_ANY || child->pid == __pid) {
                    has_matching_child = true;
                    if (child->state == ProcessState::Zombie || child->state == ProcessState::Terminated ||
                        child->state == ProcessState::Faulted) {
                        match = child;
                        break;
                    }
                }
                child_pid = child->next_sibling_pid;
            }

            if (match != nullptr)
                break;
            if (!has_matching_child)
                return WAIT_ECHILD;

            parent->parent_waiting = true;
            const int wait_result = bigos::sched::wait_queue_wait_until(&g_process_wait_queue, nullptr, nullptr, 0);
            parent->parent_waiting = false;
            if (wait_result == bigos::sched::WAIT_BLOCK_FORBIDDEN)
                return WAIT_EWOULDBLOCK;
            if (wait_result != bigos::sched::WAIT_OK)
                return WAIT_EINVAL;
        }

        if (__status != nullptr)
            *__status = match->exit_code;
        const uint32_t waited_pid = match->pid;
        match->wait_status_consumed = true;
        mark_reap_pending(match);
        return (int64_t)waited_pid;
    }

    void reap_pending_processes() noexcept {
        uint32_t *link = &g_reap_head_pid;
        while (*link != 0) {
            Process *process = lookup_process(*link);
            if (process == nullptr) {
                *link = 0;
                continue;
            }
            if (!process->reap_pending || process->resources_reclaimed) {
                link = &process->next_reap_pid;
                continue;
            }
            if (stack_is_active(process)) {
                bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-stack\n");
                link = &process->next_reap_pid;
                continue;
            }
            if (process->address_space_root == (bigos::mm::read_cr3() & 0x000ffffffffff000ull)) {
                bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-root\n");
                link = &process->next_reap_pid;
                continue;
            }

            if (process->address_space_root != bigos::mm::INVALID_PHYS_ADDR) {
                if (!bigos::mm::teardown_user_address_space(process->address_space_root)) {
                    bigos::serial_puts("BIGOS_USER_REAP_FAILED address-space\n");
                    link = &process->next_reap_pid;
                    continue;
                }
                process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
            }

            close_all_fds(process);

            if (process->kernel_stack_base != nullptr) {
                bigos::free_pages(process->kernel_stack_base);
                process->kernel_stack_base = nullptr;
                process->kernel_stack_len = 0;
                process->kernel_stack_top = 0;
            }

            process->resources_reclaimed = true;
            process->reap_pending = false;
            process->state = ProcessState::Reaped;
            *link = process->next_reap_pid;
            process->next_reap_pid = 0;
            unpublish_process(process);
            bigos::serial_puts("BIGOS_USER_RECLAIMED\n");
        }
    }

#ifdef BIGOS_USER_PROGRAM_SMOKE
    void user_program_smoke_entry(void *) noexcept {
        static Process first_process;
        if (!create_first_user_process(&first_process))
            halt_failed("BIGOS_USER_LOAD_FAILED create\n");
        run_user_process(&first_process);
    }
#endif
}   // namespace bigos::proc
