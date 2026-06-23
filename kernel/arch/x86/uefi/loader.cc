#include "uefi.h"

#include <arch/x86/boot/boot_info.h>
#include <bigos/elf.h>

#define KERNEL_VMA_BASE 0xffffffff80000000ull
#define KERNEL_PHYS_BASE 0x1000000ull
#define KERNEL_MAX_LOAD_BYTES 0x4000000ull
#define UEFI_MEMORY_MAP_BYTES 0x8000ull
#define UEFI_BOOT_INFO_BYTES BIGOS_BOOT_INFO_V2_MAX_SIZE
#define UEFI_STACK_PAGES 8ull
#define UEFI_LOW_ALLOC_MAX 0x3fffffffull
#define UEFI_FIXED_PML4 0x2000ull
#define UEFI_FIXED_PDPT 0x3000ull
#define UEFI_FIXED_LOW_PD 0x4000ull
#define UEFI_FIXED_HIGH_PD 0x5000ull
#define UEFI_FIXED_PAGE_TABLE_BYTES 0x4000ull
#define UEFI_FONT_MAX_BYTES BIGOS_BOOT_FONT_ASSET_MAX_BYTES
#define EM_X86_64 62
#define EV_CURRENT 1

extern "C" [[noreturn]] void uefi_handoff(uint64_t cr3, uint64_t stack_top, uint64_t entry, const BootInfoHeader *boot_info);

struct LoadedKernel {
    uint8_t *file;
    uint64_t file_size;
    uint64_t entry;
    uint64_t memory_size;
};

struct FinalMemoryMap {
    uint8_t storage[UEFI_MEMORY_MAP_BYTES] __attribute__((aligned(8)));
    UINTN size;
    UINTN key;
    UINTN descriptor_size;
    uint32_t descriptor_version;
};

struct FramebufferHandoff {
    bool available;
    BootFramebufferMetadata metadata;
};

struct FontAssetHandoff {
    bool available;
    BootFontAssetMetadata metadata;
};

static EFI_SYSTEM_TABLE *g_system_table;
static EFI_HANDLE g_image_handle;
static FinalMemoryMap g_final_map;
static BootMemoryRegion g_preserved_regions[96];

static void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_print(const char *text) {
    while (*text != 0) {
        outb(0x3f8, (uint8_t)*text++);
    }
}

static void console_print(const CHAR16 *text) {
    if (g_system_table != nullptr && g_system_table->con_out != nullptr && g_system_table->con_out->output_string != nullptr) {
        g_system_table->con_out->output_string(g_system_table->con_out, text);
    }
}

static void print(const char *serial, const CHAR16 *console) {
    serial_print(serial);
    console_print(console);
}

static void serial_print_hex(uint64_t value) {
    serial_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> shift) & 0xfu);
        char out[2] = {(char)(digit < 10 ? '0' + digit : 'a' + digit - 10), 0};
        serial_print(out);
    }
}

static void serial_print_dec(uint64_t value) {
    char buffer[21];
    uint32_t index = sizeof(buffer);
    buffer[--index] = 0;
    do {
        buffer[--index] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && index > 0);
    serial_print(&buffer[index]);
}

[[noreturn]] static void fail(const char *serial, const CHAR16 *console) {
    print(serial, console);
    for (;;) {
        asm volatile("cli; hlt");
    }
}

static void *memset_local(void *dest, int value, uint64_t size) {
    uint8_t *out = (uint8_t *)dest;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = (uint8_t)value;
    }
    return dest;
}

static void *memcpy_local(void *dest, const void *src, uint64_t size) {
    uint8_t *out = (uint8_t *)dest;
    const uint8_t *in = (const uint8_t *)src;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
    return dest;
}

extern "C" void *memset(void *dest, int value, uint64_t size) {
    return memset_local(dest, value, size);
}

extern "C" void *memcpy(void *dest, const void *src, uint64_t size) {
    return memcpy_local(dest, src, size);
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) & ~(alignment - 1);
}

static bool efi_error(EFI_STATUS status) {
    return (status & 0x8000000000000000ull) != 0;
}

static void copy_ascii(char *dest, uint64_t dest_size, const char *src) {
    if (dest_size == 0) {
        return;
    }
    uint64_t i = 0;
    while (i + 1 < dest_size && src[i] != 0) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

static void copy_char16_ascii(char *dest, uint64_t dest_size, const CHAR16 *src) {
    if (dest_size == 0) {
        return;
    }
    uint64_t i = 0;
    while (i + 1 < dest_size && src != nullptr && src[i] != 0) {
        CHAR16 ch = src[i];
        dest[i] = ch < 0x80 ? (char)ch : '?';
        i++;
    }
    dest[i] = 0;
}

static EFI_BOOT_SERVICES *bs() {
    return g_system_table->boot_services;
}

static EFI_PHYSICAL_ADDRESS allocate_low_pages(UINTN pages, const char *stage, const CHAR16 *console_stage) {
    EFI_PHYSICAL_ADDRESS address = UEFI_LOW_ALLOC_MAX;
    EFI_STATUS status = bs()->allocate_pages(AllocateMaxAddress, EfiLoaderData, pages, &address);
    if (efi_error(status)) {
        (void)stage;
        fail("BIGOS_UEFI_LOADER_ERROR allocate low pages\r\n", console_stage);
    }
    return address;
}

static EFI_PHYSICAL_ADDRESS allocate_address_pages(EFI_PHYSICAL_ADDRESS address, UINTN pages) {
    EFI_PHYSICAL_ADDRESS requested = address;
    EFI_STATUS status = bs()->allocate_pages(AllocateAddress, EfiLoaderData, pages, &requested);
    if (efi_error(status) || requested != address) {
        fail("BIGOS_UEFI_LOADER_ERROR allocate kernel window\r\n", u"BigOS UEFI: failed to allocate kernel load window\r\n");
    }
    return requested;
}

static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *open_simple_file_system() {
    void *loaded_image_raw = nullptr;
    EFI_STATUS status = bs()->handle_protocol(g_image_handle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, &loaded_image_raw);
    if (efi_error(status) || loaded_image_raw == nullptr) {
        fail("BIGOS_UEFI_LOADER_ERROR loaded image protocol\r\n", u"BigOS UEFI: LoadedImage protocol unavailable\r\n");
    }

    auto *loaded_image = (EFI_LOADED_IMAGE_PROTOCOL *)loaded_image_raw;
    void *fs_raw = nullptr;
    status = bs()->handle_protocol(loaded_image->device_handle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, &fs_raw);
    if (efi_error(status) || fs_raw == nullptr) {
        fail("BIGOS_UEFI_LOADER_ERROR simple file system\r\n", u"BigOS UEFI: SimpleFileSystem protocol unavailable\r\n");
    }
    return (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *)fs_raw;
}

static uint8_t *read_file_from_esp(const CHAR16 *path, uint64_t *size_out, bool required, uint64_t max_size) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = open_simple_file_system();
    EFI_FILE_PROTOCOL *root = nullptr;
    EFI_STATUS status = fs->open_volume(fs, &root);
    if (efi_error(status) || root == nullptr) {
        fail("BIGOS_UEFI_LOADER_ERROR open volume\r\n", u"BigOS UEFI: failed to open ESP volume\r\n");
    }

    EFI_FILE_PROTOCOL *file = nullptr;
    status = root->open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (efi_error(status) || file == nullptr) {
        root->close(root);
        if (required) {
            fail("BIGOS_UEFI_LOADER_ERROR open file\r\n", u"BigOS UEFI: failed to open required ESP file\r\n");
        }
        return nullptr;
    }

    status = file->set_position(file, 0xffffffffffffffffull);
    uint64_t file_size = 0;
    if (!efi_error(status)) {
        status = file->get_position(file, &file_size);
    }
    if (efi_error(status) || file_size == 0 || file_size > max_size) {
        file->close(file);
        root->close(root);
        if (required) {
            fail("BIGOS_UEFI_LOADER_ERROR file size\r\n", u"BigOS UEFI: failed to measure required ESP file\r\n");
        }
        return nullptr;
    }
    status = file->set_position(file, 0);
    if (efi_error(status)) {
        file->close(file);
        root->close(root);
        if (required) {
            fail("BIGOS_UEFI_LOADER_ERROR file seek\r\n", u"BigOS UEFI: failed to rewind required ESP file\r\n");
        }
        return nullptr;
    }

    void *buffer = nullptr;
    status = bs()->allocate_pool(EfiLoaderData, file_size, &buffer);
    if (efi_error(status) || buffer == nullptr) {
        file->close(file);
        root->close(root);
        if (required) {
            fail("BIGOS_UEFI_LOADER_ERROR file buffer\r\n", u"BigOS UEFI: failed to allocate ESP file buffer\r\n");
        }
        return nullptr;
    }

    UINTN read_size = file_size;
    status = file->read(file, &read_size, buffer);
    if (efi_error(status) || read_size != file_size) {
        file->close(file);
        root->close(root);
        if (required) {
            fail("BIGOS_UEFI_LOADER_ERROR file read\r\n", u"BigOS UEFI: failed to read required ESP file\r\n");
        }
        return nullptr;
    }
    file->close(file);
    root->close(root);
    *size_out = file_size;
    return (uint8_t *)buffer;
}

static bool validate_elf_header(const Elf64_Ehdr *ehdr, uint64_t file_size) {
    return file_size >= sizeof(Elf64_Ehdr) && ehdr->e_ident[EI_MAG0] == 0x7f && ehdr->e_ident[EI_MAG1] == 'E' &&
           ehdr->e_ident[EI_MAG2] == 'L' && ehdr->e_ident[EI_MAG3] == 'F' && ehdr->e_ident[EI_CLASS] == ELFCLASS64 &&
           ehdr->e_ident[EI_DATA] == ELFDATA2LSB && ehdr->e_ident[EI_VERSION] == EV_CURRENT && ehdr->e_type == ET_EXEC &&
           ehdr->e_machine == EM_X86_64 && ehdr->e_ehsize == sizeof(Elf64_Ehdr) && ehdr->e_phentsize == sizeof(Elf64_Phdr) &&
           ehdr->e_phoff >= sizeof(Elf64_Ehdr) && ehdr->e_phnum > 0;
}

static bool validate_segment_target(const Elf64_Phdr *phdr) {
    if (phdr->p_filesz > phdr->p_memsz || phdr->p_vaddr < KERNEL_VMA_BASE) {
        return false;
    }
    uint64_t start = phdr->p_vaddr - KERNEL_VMA_BASE;
    uint64_t end = start + phdr->p_memsz;
    return end >= start && end <= KERNEL_MAX_LOAD_BYTES;
}

static bool validate_segment_file_range(const Elf64_Phdr *phdr, uint64_t file_size) {
    uint64_t end = phdr->p_offset + phdr->p_filesz;
    return end >= phdr->p_offset && end <= file_size;
}

static LoadedKernel load_kernel_elf() {
    uint64_t file_size = 0;
    uint8_t *file = read_file_from_esp(u"\\boot\\kernel", &file_size, true, KERNEL_MAX_LOAD_BYTES);
    auto *ehdr = (const Elf64_Ehdr *)file;
    if (!validate_elf_header(ehdr, file_size)) {
        fail("BIGOS_UEFI_LOADER_ERROR invalid kernel\r\n", u"BigOS UEFI: invalid kernel ELF\r\n");
    }

    uint64_t phdr_bytes = (uint64_t)ehdr->e_phentsize * ehdr->e_phnum;
    if (ehdr->e_phoff + phdr_bytes < ehdr->e_phoff || ehdr->e_phoff + phdr_bytes > file_size) {
        fail("BIGOS_UEFI_LOADER_ERROR invalid phdr\r\n", u"BigOS UEFI: invalid kernel program headers\r\n");
    }

    const auto *phdrs = (const Elf64_Phdr *)(file + ehdr->e_phoff);
    uint64_t kernel_memory_size = 0;
    bool has_load = false;
    bool entry_in_load = false;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        has_load = true;
        if (!validate_segment_target(phdr) || !validate_segment_file_range(phdr, file_size)) {
            fail("BIGOS_UEFI_LOADER_ERROR invalid segment\r\n", u"BigOS UEFI: invalid kernel segment\r\n");
        }
        uint64_t end = phdr->p_vaddr - KERNEL_VMA_BASE + phdr->p_memsz;
        if (end > kernel_memory_size) {
            kernel_memory_size = end;
        }
        if (ehdr->e_entry >= phdr->p_vaddr && ehdr->e_entry < phdr->p_vaddr + phdr->p_memsz) {
            entry_in_load = true;
        }
    }
    if (!has_load || !entry_in_load || kernel_memory_size == 0) {
        fail("BIGOS_UEFI_LOADER_ERROR invalid entry\r\n", u"BigOS UEFI: invalid kernel entry\r\n");
    }

    return {file, file_size, ehdr->e_entry, kernel_memory_size};
}

static void materialize_kernel_segments(const LoadedKernel &kernel) {
    const auto *ehdr = (const Elf64_Ehdr *)kernel.file;
    const auto *phdrs = (const Elf64_Phdr *)(kernel.file + ehdr->e_phoff);
    memset_local((void *)KERNEL_PHYS_BASE, 0, align_up_u64(kernel.memory_size, EFI_PAGE_SIZE));
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        uint64_t phys = KERNEL_PHYS_BASE + (phdr->p_vaddr - KERNEL_VMA_BASE);
        memcpy_local((void *)phys, kernel.file + phdr->p_offset, phdr->p_filesz);
    }
}

static uint32_t normalize_gop_pixel_format(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info) {
    if (info == nullptr)
        return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_UNKNOWN;
    switch (info->pixel_format) {
        case PixelBlueGreenRedReserved8BitPerColor:
            return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_BGRX8888;
        case PixelRedGreenBlueReserved8BitPerColor:
            return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_RGBX8888;
        case PixelBitMask:
            if (info->pixel_information.red_mask == 0x00ff0000u && info->pixel_information.green_mask == 0x0000ff00u &&
                info->pixel_information.blue_mask == 0x000000ffu)
                return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_BGRX8888;
            if (info->pixel_information.red_mask == 0x000000ffu && info->pixel_information.green_mask == 0x0000ff00u &&
                info->pixel_information.blue_mask == 0x00ff0000u)
                return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_RGBX8888;
            return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_UNKNOWN;
        default:
            return BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_UNKNOWN;
    }
}

static FramebufferHandoff prepare_framebuffer_handoff() {
    void *gop_raw = nullptr;
    EFI_STATUS status = bs()->locate_protocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nullptr, &gop_raw);
    if (efi_error(status) || gop_raw == nullptr) {
        print("BIGOS_UEFI_FRAMEBUFFER unavailable stage=locate_gop\r\n", u"BigOS UEFI: GOP unavailable; framebuffer fallback\r\n");
        return {};
    }

    auto *gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)gop_raw;
    if (gop->mode == nullptr || gop->mode->info == nullptr || gop->mode->frame_buffer_base == 0 ||
        gop->mode->frame_buffer_size == 0) {
        print("BIGOS_UEFI_FRAMEBUFFER unavailable stage=gop_mode\r\n", u"BigOS UEFI: GOP mode unusable; framebuffer fallback\r\n");
        return {};
    }

    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->mode->info;
    uint32_t format = normalize_gop_pixel_format(info);
    if (format == BIGOS_BOOT_FRAMEBUFFER_PIXEL_FORMAT_UNKNOWN || info->horizontal_resolution == 0 ||
        info->vertical_resolution == 0 || info->pixels_per_scan_line < info->horizontal_resolution) {
        print("BIGOS_UEFI_FRAMEBUFFER unavailable stage=pixel_format\r\n",
              u"BigOS UEFI: GOP pixel format unsupported; framebuffer fallback\r\n");
        return {};
    }

    FramebufferHandoff handoff = {};
    handoff.available = true;
    handoff.metadata = {
        gop->mode->frame_buffer_base,
        gop->mode->frame_buffer_size,
        info->horizontal_resolution,
        info->vertical_resolution,
        info->pixels_per_scan_line,
        32,
        4,
        format,
        BIGOS_BOOT_FRAMEBUFFER_ATTR_FIRMWARE_GOP | BIGOS_BOOT_FRAMEBUFFER_ATTR_WRITE_COMBINE,
    };
    if (!bigos_boot_framebuffer_metadata_valid(&handoff.metadata)) {
        print("BIGOS_UEFI_FRAMEBUFFER unavailable stage=metadata_validate\r\n",
              u"BigOS UEFI: GOP metadata invalid; framebuffer fallback\r\n");
        return {};
    }

    serial_print("BIGOS_UEFI_FRAMEBUFFER base=");
    serial_print_hex(handoff.metadata.physical_base);
    serial_print(" size=");
    serial_print_hex(handoff.metadata.byte_size);
    serial_print(" width=");
    serial_print_dec(handoff.metadata.width);
    serial_print(" height=");
    serial_print_dec(handoff.metadata.height);
    serial_print(" stride=");
    serial_print_dec(handoff.metadata.pixels_per_scanline);
    serial_print(" format=");
    serial_print_dec(handoff.metadata.pixel_format);
    serial_print("\r\n");
    return handoff;
}

static FontAssetHandoff prepare_font_asset_handoff() {
    uint64_t file_size = 0;
    uint8_t *file = read_file_from_esp(u"\\boot\\fonts\\unifont.bin", &file_size, false, UEFI_FONT_MAX_BYTES);
    if (file == nullptr) {
        print("BIGOS_UEFI_FONT unavailable stage=open_font\r\n", u"BigOS UEFI: font asset unavailable; font fallback\r\n");
        return {};
    }
    if (file_size < sizeof(BootFontAssetHeader)) {
        print("BIGOS_UEFI_FONT unavailable stage=font_size\r\n", u"BigOS UEFI: font asset too small; font fallback\r\n");
        return {};
    }

    const auto *header = (const BootFontAssetHeader *)file;
    if (header->magic != BIGOS_BOOT_FONT_ASSET_MAGIC || header->header_size < sizeof(BootFontAssetHeader) ||
        header->format_version != BIGOS_BOOT_FONT_FORMAT_UNIFONT_HEX_V1 || header->glyph_width == 0 ||
        header->glyph_height == 0 || header->cell_width == 0 || header->cell_height == 0) {
        print("BIGOS_UEFI_FONT unavailable stage=font_header\r\n", u"BigOS UEFI: font asset invalid; font fallback\r\n");
        return {};
    }

    FontAssetHandoff handoff = {};
    handoff.available = true;
    handoff.metadata = {
        (uint64_t)file,
        file_size,
        header->format_version,
        header->glyph_width,
        header->glyph_height,
        header->cell_width,
        header->cell_height,
        BIGOS_BOOT_FONT_ASSET_FLAG_LOADER_PROVIDED | BIGOS_BOOT_FONT_ASSET_FLAG_ESP_LOADED,
        header->glyph_count,
    };
    if (!bigos_boot_font_asset_metadata_valid(&handoff.metadata)) {
        print("BIGOS_UEFI_FONT unavailable stage=metadata_validate\r\n", u"BigOS UEFI: font metadata invalid; font fallback\r\n");
        return {};
    }

    serial_print("BIGOS_UEFI_FONT base=");
    serial_print_hex(handoff.metadata.physical_base);
    serial_print(" size=");
    serial_print_hex(handoff.metadata.byte_size);
    serial_print(" version=");
    serial_print_dec(handoff.metadata.format_version);
    serial_print(" cell=");
    serial_print_dec(handoff.metadata.cell_width);
    serial_print("x");
    serial_print_dec(handoff.metadata.cell_height);
    serial_print("\r\n");
    return handoff;
}

static uint64_t uefi_memory_attributes(const EFI_MEMORY_DESCRIPTOR *desc) {
    uint64_t attrs = ((uint64_t)desc->type << BIGOS_BOOT_MEMORY_ATTR_FIRMWARE_TYPE_SHIFT);
    if ((desc->attribute & EFI_MEMORY_RUNTIME) != 0) {
        attrs |= BIGOS_BOOT_MEMORY_ATTR_RUNTIME;
    }
    if ((desc->attribute & EFI_MEMORY_WB) != 0) {
        attrs |= BIGOS_BOOT_MEMORY_ATTR_WRITE_BACK;
    }
    if ((desc->attribute & EFI_MEMORY_WC) != 0) {
        attrs |= BIGOS_BOOT_MEMORY_ATTR_WRITE_COMBINE;
    }
    if ((desc->attribute & EFI_MEMORY_UC) != 0) {
        attrs |= BIGOS_BOOT_MEMORY_ATTR_UNCACHEABLE;
    }
    return attrs;
}

static uint32_t normalize_uefi_memory_type(const EFI_MEMORY_DESCRIPTOR *desc) {
    if ((desc->attribute & EFI_MEMORY_RUNTIME) != 0) {
        return BIGOS_BOOT_MEMORY_TYPE_RUNTIME;
    }
    switch (desc->type) {
        case EfiConventionalMemory:
            return BIGOS_BOOT_MEMORY_TYPE_USABLE;
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            return BIGOS_BOOT_MEMORY_TYPE_LOADER;
        case EfiACPIReclaimMemory:
            return BIGOS_BOOT_MEMORY_TYPE_ACPI_RECLAIM;
        case EfiACPIMemoryNVS:
            return BIGOS_BOOT_MEMORY_TYPE_ACPI_NVS;
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
            return BIGOS_BOOT_MEMORY_TYPE_MMIO;
        case EfiUnusableMemory:
            return BIGOS_BOOT_MEMORY_TYPE_BAD_MEMORY;
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
            return BIGOS_BOOT_MEMORY_TYPE_RUNTIME;
        default:
            return BIGOS_BOOT_MEMORY_TYPE_RESERVED;
    }
}

static EFI_MEMORY_DESCRIPTOR *descriptor_at(FinalMemoryMap *map, UINTN index) {
    return (EFI_MEMORY_DESCRIPTOR *)(map->storage + index * map->descriptor_size);
}

static BootMemoryRegion convert_descriptor(const EFI_MEMORY_DESCRIPTOR *desc) {
    return {
        desc->physical_start,
        desc->number_of_pages * EFI_PAGE_SIZE,
        normalize_uefi_memory_type(desc),
        BIGOS_BOOT_MEMORY_SOURCE_UEFI,
        uefi_memory_attributes(desc),
        desc->type,
    };
}

static bool mergeable_regions(const BootMemoryRegion &left, const BootMemoryRegion &right) {
    return left.physical_base + left.length == right.physical_base && left.normalized_type == right.normalized_type &&
           left.source_type == right.source_type && left.attributes == right.attributes &&
           left.source_value == right.source_value;
}

static void copy_boot_region(BootMemoryRegion &dest, const BootMemoryRegion &src) {
    dest.physical_base = src.physical_base;
    dest.length = src.length;
    dest.normalized_type = src.normalized_type;
    dest.source_type = src.source_type;
    dest.attributes = src.attributes;
    dest.source_value = src.source_value;
}

static uint32_t collect_boot_regions(FinalMemoryMap *map, BootMemoryRegion *out, uint32_t capacity, bool include_reserved) {
    uint32_t count = 0;
    uint32_t descriptor_count = (uint32_t)(map->size / map->descriptor_size);
    for (uint32_t i = 0; i < descriptor_count; i++) {
        BootMemoryRegion region = convert_descriptor(descriptor_at(map, i));
        if (region.length == 0) {
            continue;
        }
        if (!include_reserved && region.normalized_type != BIGOS_BOOT_MEMORY_TYPE_USABLE) {
            continue;
        }
        if (count > 0 && mergeable_regions(out[count - 1], region)) {
            out[count - 1].length += region.length;
            continue;
        }
        if (count >= capacity) {
            return capacity + 1;
        }
        copy_boot_region(out[count], region);
        count++;
    }
    return count;
}

static void refresh_memory_map(FinalMemoryMap *map) {
    map->size = sizeof(map->storage);
    EFI_STATUS status =
        bs()->get_memory_map(&map->size, (EFI_MEMORY_DESCRIPTOR *)map->storage, &map->key, &map->descriptor_size,
                             &map->descriptor_version);
    if (efi_error(status)) {
        fail("BIGOS_UEFI_LOADER_ERROR memory map\r\n", u"BigOS UEFI: failed to get memory map\r\n");
    }
    if (map->descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) || map->descriptor_size == 0) {
        fail("BIGOS_UEFI_LOADER_ERROR memory map descriptor\r\n", u"BigOS UEFI: invalid memory map descriptor\r\n");
    }
}

static BootInfoHeader *build_boot_info(
    void *boot_info_storage,
    const LoadedKernel &kernel,
    FinalMemoryMap *map,
    const FramebufferHandoff &framebuffer,
    const FontAssetHandoff &font_asset) {
    uint32_t section_count = 4 + (framebuffer.available ? 1u : 0u) + (font_asset.available ? 1u : 0u);
    uint32_t section_table_offset = align_up_u32(sizeof(BootInfoHeader), alignof(BootInfoSection));
    uint32_t core_offset =
        align_up_u32(section_table_offset + section_count * sizeof(BootInfoSection), alignof(BootInfoCore));
    uint32_t memory_map_offset = align_up_u32(core_offset + sizeof(BootInfoCore), alignof(BootMemoryRegion));
    uint32_t tail_min_offset = memory_map_offset;
    uint32_t storage_min_offset = align_up_u32(tail_min_offset, alignof(BootStorageMetadata));
    uint32_t loader_min_offset = align_up_u32(storage_min_offset + sizeof(BootStorageMetadata), alignof(BootLoaderMetadata));
    uint32_t next_min_offset = loader_min_offset + sizeof(BootLoaderMetadata);
    if (framebuffer.available)
        next_min_offset = align_up_u32(next_min_offset, alignof(BootFramebufferMetadata)) + sizeof(BootFramebufferMetadata);
    if (font_asset.available)
        next_min_offset = align_up_u32(next_min_offset, alignof(BootFontAssetMetadata)) + sizeof(BootFontAssetMetadata);
    uint32_t fixed_tail_size = next_min_offset - tail_min_offset;
    uint32_t memory_map_capacity = (UEFI_BOOT_INFO_BYTES - memory_map_offset - fixed_tail_size) / sizeof(BootMemoryRegion);

    auto *header = (BootInfoHeader *)boot_info_storage;
    auto *regions = (BootMemoryRegion *)((uint8_t *)header + memory_map_offset);
    uint32_t region_count = collect_boot_regions(map, regions, memory_map_capacity, true);
    if (region_count > memory_map_capacity) {
        region_count = collect_boot_regions(map, regions, memory_map_capacity, false);
    }
    if (region_count == 0 || region_count > memory_map_capacity) {
        fail("BIGOS_UEFI_LOADER_ERROR memory map too large\r\n", u"BigOS UEFI: memory map is too large\r\n");
    }

    uint32_t memory_map_size = region_count * sizeof(BootMemoryRegion);
    uint32_t storage_offset = align_up_u32(memory_map_offset + memory_map_size, alignof(BootStorageMetadata));
    uint32_t loader_offset = align_up_u32(storage_offset + sizeof(BootStorageMetadata), alignof(BootLoaderMetadata));
    uint32_t next_offset = loader_offset + sizeof(BootLoaderMetadata);
    uint32_t framebuffer_offset = 0;
    if (framebuffer.available) {
        framebuffer_offset = align_up_u32(next_offset, alignof(BootFramebufferMetadata));
        next_offset = framebuffer_offset + sizeof(BootFramebufferMetadata);
    }
    uint32_t font_offset = 0;
    if (font_asset.available) {
        font_offset = align_up_u32(next_offset, alignof(BootFontAssetMetadata));
        next_offset = font_offset + sizeof(BootFontAssetMetadata);
    }
    uint32_t total_size = align_up_u32(next_offset, BIGOS_BOOT_INFO_V2_ALIGNMENT);
    if (total_size > UEFI_BOOT_INFO_BYTES) {
        fail("BIGOS_UEFI_LOADER_ERROR boot info too large\r\n", u"BigOS UEFI: BootInfo is too large\r\n");
    }

    if (region_count > 96) {
        fail("BIGOS_UEFI_LOADER_ERROR memory map too large\r\n", u"BigOS UEFI: memory map is too large\r\n");
    }
    memcpy_local(g_preserved_regions, regions, region_count * sizeof(BootMemoryRegion));
    memset_local(header, 0, UEFI_BOOT_INFO_BYTES);
    header->magic = BIGOS_BOOT_INFO_V2_MAGIC;
    header->version = BIGOS_BOOT_INFO_V2_VERSION;
    header->header_size = sizeof(BootInfoHeader);
    header->total_size = total_size;
    header->flags = BIGOS_BOOT_CORE_FLAG_UEFI;
    header->boot_protocol = BIGOS_BOOT_PROTOCOL_UEFI;
    header->section_count = section_count;
    header->section_table_offset = section_table_offset;

    auto *sections = (BootInfoSection *)((uint8_t *)header + section_table_offset);
    sections[0] = {BIGOS_BOOT_SECTION_TYPE_CORE, BIGOS_BOOT_SECTION_FLAG_REQUIRED, core_offset, sizeof(BootInfoCore),
                   alignof(BootInfoCore), 0};
    sections[1] = {BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP, BIGOS_BOOT_SECTION_FLAG_REQUIRED, memory_map_offset,
                   memory_map_size, alignof(BootMemoryRegion), 0};
    sections[2] = {BIGOS_BOOT_SECTION_TYPE_STORAGE_METADATA, 0, storage_offset, sizeof(BootStorageMetadata),
                   alignof(BootStorageMetadata), 0};
    sections[3] = {BIGOS_BOOT_SECTION_TYPE_LOADER_METADATA, 0, loader_offset, sizeof(BootLoaderMetadata),
                   alignof(BootLoaderMetadata), 0};
    uint32_t section_index = 4;
    if (framebuffer.available) {
        sections[section_index++] = {BIGOS_BOOT_SECTION_TYPE_FRAMEBUFFER_METADATA, 0, framebuffer_offset,
                                     sizeof(BootFramebufferMetadata), alignof(BootFramebufferMetadata), 0};
    }
    if (font_asset.available) {
        sections[section_index++] = {BIGOS_BOOT_SECTION_TYPE_FONT_ASSET_METADATA, 0, font_offset,
                                     sizeof(BootFontAssetMetadata), alignof(BootFontAssetMetadata), 0};
    }

    auto *core = (BootInfoCore *)((uint8_t *)header + core_offset);
    core->flags = BIGOS_BOOT_CORE_FLAG_UEFI;
    core->boot_protocol = BIGOS_BOOT_PROTOCOL_UEFI;
    core->boot_drive = 0;
    core->exfat_data_area_lba = 0;
    core->kernel_load_vaddr = KERNEL_VMA_BASE;
    core->kernel_entry_vaddr = kernel.entry;
    core->kernel_file_size = kernel.file_size;
    core->kernel_memory_size = kernel.memory_size;

    regions = (BootMemoryRegion *)((uint8_t *)header + memory_map_offset);
    memcpy_local(regions, g_preserved_regions, region_count * sizeof(BootMemoryRegion));

    auto *storage = (BootStorageMetadata *)((uint8_t *)header + storage_offset);
    storage->kind = BIGOS_BOOT_STORAGE_KIND_UEFI_ESP;
    storage->source_id = 0;
    copy_ascii(storage->boot_path, sizeof(storage->boot_path), "\\EFI\\BOOT\\BOOTX64.EFI");
    copy_ascii(storage->root_path, sizeof(storage->root_path), "\\");

    auto *loader = (BootLoaderMetadata *)((uint8_t *)header + loader_offset);
    loader->backend = BIGOS_BOOT_LOADER_BACKEND_X86_64_UEFI;
    loader->loader_version = 1;
    loader->firmware_revision = g_system_table->firmware_revision;
    copy_ascii(loader->build_id, sizeof(loader->build_id), "bigos-uefi-spike-v1");
    copy_char16_ascii(loader->firmware_vendor, sizeof(loader->firmware_vendor), g_system_table->firmware_vendor);
    copy_ascii(loader->boot_file_path, sizeof(loader->boot_file_path), "\\boot\\kernel");

    if (framebuffer.available) {
        auto *fb = (BootFramebufferMetadata *)((uint8_t *)header + framebuffer_offset);
        *fb = framebuffer.metadata;
    }
    if (font_asset.available) {
        auto *font = (BootFontAssetMetadata *)((uint8_t *)header + font_offset);
        *font = font_asset.metadata;
    }

    if (!bigos_boot_info_v2_validate(header)) {
        fail("BIGOS_UEFI_LOADER_ERROR invalid boot info\r\n", u"BigOS UEFI: invalid BootInfo v2\r\n");
    }
    return header;
}

static uint64_t *build_fixed_page_tables() {
    memset_local((void *)UEFI_FIXED_PML4, 0, UEFI_FIXED_PAGE_TABLE_BYTES);
    auto *pml4 = (uint64_t *)UEFI_FIXED_PML4;
    auto *pdpt = (uint64_t *)UEFI_FIXED_PDPT;
    auto *low_pd = (uint64_t *)UEFI_FIXED_LOW_PD;
    auto *high_pd = (uint64_t *)UEFI_FIXED_HIGH_PD;

    constexpr uint64_t present_rw = 0x3;
    constexpr uint64_t present_rw_large = 0x83;
    pml4[0] = UEFI_FIXED_PDPT | present_rw;
    pml4[256] = UEFI_FIXED_PML4 | present_rw;
    pml4[511] = UEFI_FIXED_PDPT | present_rw;
    pdpt[0] = UEFI_FIXED_LOW_PD | present_rw;
    pdpt[510] = UEFI_FIXED_HIGH_PD | present_rw;
    for (uint64_t i = 0; i < 512; i++) {
        low_pd[i] = (i * 0x200000ull) | present_rw_large;
        high_pd[i] = (KERNEL_PHYS_BASE + i * 0x200000ull) | present_rw_large;
    }
    return pml4;
}

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    g_image_handle = image_handle;
    g_system_table = system_table;
    print("BIGOS_UEFI_LOADER_START\r\n", u"BigOS UEFI loader start\r\n");

    LoadedKernel kernel = load_kernel_elf();
    FramebufferHandoff framebuffer = prepare_framebuffer_handoff();
    FontAssetHandoff font_asset = prepare_font_asset_handoff();
    EFI_PHYSICAL_ADDRESS stack = allocate_low_pages(UEFI_STACK_PAGES, "stack", u"BigOS UEFI: failed to allocate stack\r\n");
    EFI_PHYSICAL_ADDRESS boot_info_storage = allocate_low_pages(1, "boot info",
                                                                u"BigOS UEFI: failed to allocate BootInfo\r\n");

    refresh_memory_map(&g_final_map);
    BootInfoHeader *boot_info =
        build_boot_info((void *)boot_info_storage, kernel, &g_final_map, framebuffer, font_asset);
    EFI_STATUS status = bs()->exit_boot_services(g_image_handle, g_final_map.key);
    if (efi_error(status)) {
        refresh_memory_map(&g_final_map);
        boot_info = build_boot_info((void *)boot_info_storage, kernel, &g_final_map, framebuffer, font_asset);
        status = bs()->exit_boot_services(g_image_handle, g_final_map.key);
        if (efi_error(status)) {
            fail("BIGOS_UEFI_LOADER_ERROR exit boot services\r\n", u"BigOS UEFI: ExitBootServices failed\r\n");
        }
    }

    materialize_kernel_segments(kernel);
    uint64_t *pml4 = build_fixed_page_tables();
    uint64_t stack_top = stack + UEFI_STACK_PAGES * EFI_PAGE_SIZE - 16;
    uefi_handoff((uint64_t)pml4, stack_top, kernel.entry, boot_info);
}
