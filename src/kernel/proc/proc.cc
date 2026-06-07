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
    constexpr uint32_t FIRST_PID = 1;
    constexpr uint32_t ELF_PID = 2;
    constexpr uint32_t PROCESS_KERNEL_STACK_PAGES = 1;
    constexpr uint32_t ELF_MAX_LOAD_SEGMENTS = 8;
    constexpr uint32_t ELF_MAX_TRACKED_PAGES = 32;
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
        0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00,               // mov $SYS_WRITE,%rax
        0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,               // mov $1,%rdi
        0x48, 0x8d, 0x35, 0x18, 0x00, 0x00, 0x00,               // lea message(%rip),%rsi
        0x48, 0xc7, 0xc2, 0x11, 0x00, 0x00, 0x00,               // mov $17,%rdx
        0xcd, 0x80,                                             // int $0x80
        0x48, 0xc7, 0xc0, 0x03, 0x00, 0x00, 0x00,               // mov $SYS_EXIT,%rax
        0x48, 0x31, 0xff,                                       // xor %rdi,%rdi
        0xcd, 0x80,                                             // int $0x80
        0xf4, 0xeb, 0xfd,                                       // hlt; jmp .
        'B',  'I',  'G',  'O',  'S',  '_',  'U',  'S',  'E',
        'R',  '_',  'W',  'R',  'I',  'T',  'E',  '\n',
    };

    bigos::proc::Process *g_current_process = nullptr;
    bigos::proc::Process *g_reap_pending_process = nullptr;
    bool g_user_mode_initialized = false;

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

    bool track_page(
        TrackedPage *__pages, uint32_t *__count, uint64_t __vaddr, bigos::mm::PageAttr __attr) noexcept {
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

    bigos::proc::UserElfLoadError validate_elf(
        const uint8_t *__image, uint64_t __image_len, LoadSegment *__segments, uint32_t *__segment_count,
        uint64_t *__entry) noexcept {
        if (__image == nullptr || __image_len < sizeof(Elf64Header) ||
            __image_len > bigos::proc::USER_ELF_MAX_FILE_BYTES)
            return bigos::proc::UserElfLoadError::InvalidArgument;

        const auto *ehdr = (const Elf64Header *)__image;
        if (*(const uint32_t *)ehdr->ident != ELF_MAGIC || ehdr->ident[4] != 2 || ehdr->ident[5] != 1 ||
            ehdr->ident[6] != ELF_VERSION_CURRENT)
            return bigos::proc::UserElfLoadError::UnsupportedElf;
        if (ehdr->type != ELF_TYPE_EXEC || ehdr->machine != ELF_MACHINE_X86_64 ||
            ehdr->version != ELF_VERSION_CURRENT || ehdr->ehsize != sizeof(Elf64Header) ||
            ehdr->phentsize != sizeof(Elf64ProgramHeader) || ehdr->phnum == 0 ||
            ehdr->phnum > ELF_MAX_LOAD_SEGMENTS)
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
            segment = {phdr->vaddr, phdr->memsz, phdr->filesz, phdr->offset, map_base, map_end - map_base,
                       phdr->flags, attr};
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
        uint64_t __root, const uint8_t *__image, const LoadSegment &__segment) noexcept {
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

            if (!bigos::mm::map_page_in_root(__root, page, phys, __segment.attr))
                goto fail;
            if (!bigos::mm::map_page(page, phys, __segment.attr))
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
        process->reap_pending = true;
        g_reap_pending_process = process;
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
}   // namespace

namespace bigos::proc {
    bool create_first_user_process(Process *__process) noexcept {
        if (__process == nullptr || sizeof(FIRST_USER_CODE) > PAGE_SIZE)
            return false;

        memset(__process, 0, sizeof(*__process));
        __process->pid = FIRST_PID;
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

        code_phys = alloc_user_frame();
        data_phys = alloc_user_frame();
        stack_phys = alloc_user_frame();
        if (code_phys == 0 || data_phys == 0 || stack_phys == 0)
            goto fail;
        if (!copy_to_frame(code_phys, FIRST_USER_CODE, sizeof(FIRST_USER_CODE)) || !zero_frame(data_phys) ||
            !zero_frame(stack_phys))
            goto fail;

        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, USER_CODE_BASE, code_phys, bigos::mm::page_attr::USER_CODE))
            goto fail;
        if (!bigos::mm::map_page(USER_CODE_BASE, code_phys, bigos::mm::page_attr::USER_CODE))
            goto fail;
        code_mapped = true;
        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, USER_DATA_BASE, data_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        if (!bigos::mm::map_page(USER_DATA_BASE, data_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        data_mapped = true;
        if (!bigos::mm::map_page_in_root(
                __process->address_space_root, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        if (!bigos::mm::map_page(stack_base, stack_phys, bigos::mm::page_attr::USER_DATA))
            goto fail;
        stack_mapped = true;

        __process->code_phys = code_phys;
        __process->data_phys = data_phys;
        __process->stack_phys = stack_phys;
        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top =
            (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
        __process->entry = USER_CODE_BASE;
        __process->code = {USER_CODE_BASE, PAGE_SIZE};
        __process->data = {USER_DATA_BASE, PAGE_SIZE};
        __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
        __process->state = ProcessState::Created;
        __process->reap_pending = false;
        __process->resources_reclaimed = false;
        __process->exit_code = 0;
        __process->fault_reason = 0;
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

    UserElfLoadError create_elf_user_process(Process *__process, const void *__image, uint64_t __image_len) noexcept {
        if (__process == nullptr)
            return UserElfLoadError::InvalidArgument;
        LoadSegment segments[ELF_MAX_LOAD_SEGMENTS] = {};
        uint32_t segment_count = 0;
        uint64_t entry = 0;
        UserElfLoadError error = validate_elf((const uint8_t *)__image, __image_len, segments, &segment_count, &entry);
        if (error != UserElfLoadError::Success)
            return error;

        memset(__process, 0, sizeof(*__process));
        __process->pid = ELF_PID;
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

        for (uint32_t i = 0; i < segment_count; i++) {
            error = map_segment(__process->address_space_root, (const uint8_t *)__image, segments[i]);
            if (error != UserElfLoadError::Success)
                goto fail;
        }

        {
            const uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
            const uint64_t stack_phys = alloc_user_frame();
            if (stack_phys == 0) {
                error = UserElfLoadError::OutOfMemory;
                goto fail;
            }
            if (!zero_frame(stack_phys)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::CopyFailed;
                goto fail;
            }
            if (!bigos::mm::map_page_in_root(
                    __process->address_space_root, stack_base, stack_phys, bigos::mm::page_attr::USER_DATA)) {
                free_user_frame(stack_phys);
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            if (!bigos::mm::map_page(stack_base, stack_phys, bigos::mm::page_attr::USER_DATA)) {
                error = UserElfLoadError::MapFailed;
                goto fail;
            }
            __process->stack_phys = stack_phys;
            __process->stack = {stack_base, USER_STACK_PAGES * PAGE_SIZE};
        }

        __process->kernel_stack_len = (uint64_t)PROCESS_KERNEL_STACK_PAGES * PAGE_SIZE;
        __process->kernel_stack_top =
            (uint64_t)__process->kernel_stack_base + __process->kernel_stack_len;
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
        return UserElfLoadError::Success;

    fail:
        if (__process->kernel_stack_base != nullptr)
            bigos::free_pages(__process->kernel_stack_base);
        if (__process->address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            (void)bigos::mm::teardown_user_address_space(__process->address_space_root);
        memset(__process, 0, sizeof(*__process));
        __process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;
        return error;
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
        bigos::arch::x86::enter_user_mode(__process->entry, __process->stack.base + __process->stack.len);
    }

    Process *current_process() noexcept {
        return g_current_process;
    }

    bool validate_user_buffer(uint64_t __addr, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_WRITE_MAX_LEN)
            return false;
        return bigos::mm::user_range_mapped(process->address_space_root, __addr, __len);
    }

    bool copy_current_user_buffer(uint64_t __addr, void *__dst, uint64_t __len) noexcept {
        Process *process = g_current_process;
        if (process == nullptr || process->state != ProcessState::Running || __len > bigos::sys::SYS_WRITE_MAX_LEN)
            return false;
        return bigos::mm::__detail::copy_from_user_root(process->address_space_root, __addr, __dst, __len);
    }

    void mark_current_faulted(int64_t __reason) noexcept {
        Process *process = g_current_process;
        if (process == nullptr)
            return;
        process->state = ProcessState::Faulted;
        process->exit_code = __reason;
        process->fault_reason = __reason;
        mark_reap_pending(process);
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
        mark_reap_pending(process);
        bigos::serial_puts("BIGOS_USER_EXIT\n");
        if (process->kernel_address_space_root != bigos::mm::INVALID_PHYS_ADDR)
            bigos::mm::activate_address_space_root(process->kernel_address_space_root);
        g_current_process = nullptr;
        bigos::sched::thread_exit();
    }

    void reap_pending_processes() noexcept {
        Process *process = g_reap_pending_process;
        if (process == nullptr || !process->reap_pending || process->resources_reclaimed)
            return;
        if (stack_is_active(process)) {
            bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-stack\n");
            return;
        }
        if (process->address_space_root == (bigos::mm::read_cr3() & 0x000ffffffffff000ull)) {
            bigos::serial_puts("BIGOS_USER_REAP_DEFERRED active-root\n");
            return;
        }

        if (!bigos::mm::teardown_user_address_space(process->address_space_root)) {
            bigos::serial_puts("BIGOS_USER_REAP_FAILED address-space\n");
            return;
        }
        process->address_space_root = bigos::mm::INVALID_PHYS_ADDR;

        if (process->kernel_stack_base != nullptr) {
            bigos::free_pages(process->kernel_stack_base);
            process->kernel_stack_base = nullptr;
            process->kernel_stack_len = 0;
            process->kernel_stack_top = 0;
        }

        process->resources_reclaimed = true;
        process->reap_pending = false;
        process->state = ProcessState::Reaped;
        g_reap_pending_process = nullptr;
        bigos::serial_puts("BIGOS_USER_RECLAIMED\n");
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
