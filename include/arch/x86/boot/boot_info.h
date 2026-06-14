#ifndef _ARCH_X86_BOOT_BOOT_INFO_H
#define _ARCH_X86_BOOT_BOOT_INFO_H

#include <bigos/types.h>

#define BIGOS_BOOT_INFO_MAGIC     0x42494f53u
#define BIGOS_BOOT_INFO_VERSION   1u
#define BIGOS_BOOT_INFO_ADDRESS   0x840u
#define BIGOS_BOOT_INFO_SIZE      72u
#define BIGOS_BOOT_INFO_ALIGNMENT 8u

#define BIGOS_BOOT_INFO_V2_MAGIC     0x32494f42u
#define BIGOS_BOOT_INFO_V2_VERSION   2u
#define BIGOS_BOOT_INFO_V2_ADDRESS   0x9000u
#define BIGOS_BOOT_INFO_V2_MAX_SIZE  0x1000u
#define BIGOS_BOOT_INFO_V2_ALIGNMENT 8u

#define BIGOS_BOOT_PROTOCOL_UNKNOWN     0u
#define BIGOS_BOOT_PROTOCOL_LEGACY_BIOS 1u
#define BIGOS_BOOT_PROTOCOL_UEFI        2u

#define BIGOS_BOOT_SECTION_TYPE_CORE       1u
#define BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP 2u
#define BIGOS_BOOT_SECTION_TYPE_STORAGE_METADATA 3u
#define BIGOS_BOOT_SECTION_TYPE_LOADER_METADATA  4u

#define BIGOS_BOOT_SECTION_FLAG_REQUIRED 0x00000001u

#define BIGOS_BOOT_CORE_FLAG_LEGACY_BIOS 0x00000001u
#define BIGOS_BOOT_CORE_FLAG_UEFI        0x00000002u

#define BIGOS_BOOT_STORAGE_KIND_UNKNOWN 0u
#define BIGOS_BOOT_STORAGE_KIND_UEFI_ESP 1u

#define BIGOS_BOOT_LOADER_BACKEND_UNKNOWN     0u
#define BIGOS_BOOT_LOADER_BACKEND_X86_64_UEFI 1u

#define BIGOS_BOOT_MEMORY_TYPE_UNKNOWN      0u
#define BIGOS_BOOT_MEMORY_TYPE_USABLE       1u
#define BIGOS_BOOT_MEMORY_TYPE_RESERVED     2u
#define BIGOS_BOOT_MEMORY_TYPE_ACPI_RECLAIM 3u
#define BIGOS_BOOT_MEMORY_TYPE_ACPI_NVS     4u
#define BIGOS_BOOT_MEMORY_TYPE_MMIO         5u
#define BIGOS_BOOT_MEMORY_TYPE_LOADER       6u
#define BIGOS_BOOT_MEMORY_TYPE_KERNEL       7u
#define BIGOS_BOOT_MEMORY_TYPE_BAD_MEMORY   8u
#define BIGOS_BOOT_MEMORY_TYPE_RUNTIME      9u

#define BIGOS_BOOT_MEMORY_SOURCE_UNKNOWN   0u
#define BIGOS_BOOT_MEMORY_SOURCE_BIOS_E820 1u
#define BIGOS_BOOT_MEMORY_SOURCE_UEFI      2u

#define BIGOS_BOOT_MEMORY_ATTR_RUNTIME             0x0000000000000001ull
#define BIGOS_BOOT_MEMORY_ATTR_WRITE_BACK          0x0000000000000002ull
#define BIGOS_BOOT_MEMORY_ATTR_WRITE_COMBINE       0x0000000000000004ull
#define BIGOS_BOOT_MEMORY_ATTR_UNCACHEABLE         0x0000000000000008ull
#define BIGOS_BOOT_MEMORY_ATTR_FIRMWARE_TYPE_SHIFT 32u
#define BIGOS_BOOT_MEMORY_ATTR_FIRMWARE_TYPE_MASK  0xffffffff00000000ull

#define BIGOS_BOOT_INFO_V2_OFF_MAGIC                0u
#define BIGOS_BOOT_INFO_V2_OFF_VERSION              4u
#define BIGOS_BOOT_INFO_V2_OFF_HEADER_SIZE          6u
#define BIGOS_BOOT_INFO_V2_OFF_TOTAL_SIZE           8u
#define BIGOS_BOOT_INFO_V2_OFF_FLAGS                12u
#define BIGOS_BOOT_INFO_V2_OFF_BOOT_PROTOCOL        16u
#define BIGOS_BOOT_INFO_V2_OFF_SECTION_COUNT        18u
#define BIGOS_BOOT_INFO_V2_OFF_SECTION_TABLE_OFFSET 20u

#define BIGOS_BOOT_INFO_SECTION_OFF_TYPE      0u
#define BIGOS_BOOT_INFO_SECTION_OFF_FLAGS     4u
#define BIGOS_BOOT_INFO_SECTION_OFF_OFFSET    8u
#define BIGOS_BOOT_INFO_SECTION_OFF_SIZE      12u
#define BIGOS_BOOT_INFO_SECTION_OFF_ALIGNMENT 16u

#define BIGOS_BOOT_MEMORY_REGION_OFF_PHYSICAL_BASE   0u
#define BIGOS_BOOT_MEMORY_REGION_OFF_LENGTH          8u
#define BIGOS_BOOT_MEMORY_REGION_OFF_NORMALIZED_TYPE 16u
#define BIGOS_BOOT_MEMORY_REGION_OFF_SOURCE_TYPE     20u
#define BIGOS_BOOT_MEMORY_REGION_OFF_ATTRIBUTES      24u
#define BIGOS_BOOT_MEMORY_REGION_OFF_SOURCE_VALUE    32u

#define BIGOS_BOOT_INFO_OFF_MAGIC               0u
#define BIGOS_BOOT_INFO_OFF_VERSION             4u
#define BIGOS_BOOT_INFO_OFF_SIZE                6u
#define BIGOS_BOOT_INFO_OFF_FLAGS               8u
#define BIGOS_BOOT_INFO_OFF_HANDOFF_ADDRESS     12u
#define BIGOS_BOOT_INFO_OFF_BOOT_DRIVE          16u
#define BIGOS_BOOT_INFO_OFF_E820_ENTRY_COUNT    20u
#define BIGOS_BOOT_INFO_OFF_E820_ENTRY_ADDRESS  24u
#define BIGOS_BOOT_INFO_OFF_EXFAT_DATA_AREA_LBA 32u
#define BIGOS_BOOT_INFO_OFF_KERNEL_LOAD_VADDR   40u
#define BIGOS_BOOT_INFO_OFF_KERNEL_ENTRY_VADDR  48u
#define BIGOS_BOOT_INFO_OFF_KERNEL_FILE_SIZE    56u
#define BIGOS_BOOT_INFO_OFF_KERNEL_MEMORY_SIZE  64u

struct BootInfo {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;
    uint32_t handoff_address;
    uint8_t boot_drive;
    uint8_t reserved0[3];
    uint32_t e820_entry_count;
    uint64_t e820_entry_address;
    uint64_t exfat_data_area_lba;
    uint64_t kernel_load_vaddr;
    uint64_t kernel_entry_vaddr;
    uint64_t kernel_file_size;
    uint64_t kernel_memory_size;
};

struct BootInfoHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t flags;
    uint16_t boot_protocol;
    uint16_t section_count;
    uint32_t section_table_offset;
};

struct BootInfoSection {
    uint32_t type;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
    uint32_t alignment;
    uint32_t reserved;
};

struct BootInfoCore {
    uint32_t flags;
    uint16_t boot_protocol;
    uint8_t boot_drive;
    uint8_t reserved0;
    uint64_t exfat_data_area_lba;
    uint64_t kernel_load_vaddr;
    uint64_t kernel_entry_vaddr;
    uint64_t kernel_file_size;
    uint64_t kernel_memory_size;
};

struct BootMemoryRegion {
    uint64_t physical_base;
    uint64_t length;
    uint32_t normalized_type;
    uint32_t source_type;
    uint64_t attributes;
    uint64_t source_value;
};

struct BootStorageMetadata {
    uint32_t kind;
    uint32_t flags;
    uint64_t source_id;
    char boot_path[64];
    char root_path[64];
};

struct BootLoaderMetadata {
    uint32_t backend;
    uint32_t flags;
    uint32_t loader_version;
    uint32_t firmware_revision;
    char build_id[32];
    char firmware_vendor[64];
    char boot_file_path[64];
};

#ifdef __cplusplus
struct BootHandoff {
    const BootInfoHeader *v2;
    const BootInfo *v1;
};

inline bool bigos_boot_info_v1_valid(const BootInfo *info) {
    return info != nullptr && info->magic == BIGOS_BOOT_INFO_MAGIC && info->version == BIGOS_BOOT_INFO_VERSION &&
           info->size >= BIGOS_BOOT_INFO_SIZE;
}

inline bool bigos_boot_range_valid(uint32_t offset, uint32_t size, uint32_t total_size) {
    return offset <= total_size && size <= total_size - offset;
}

inline bool bigos_boot_aligned(uint32_t value, uint32_t alignment) {
    return alignment == 0 || (value % alignment) == 0;
}

inline bool bigos_boot_info_v2_validate(const BootInfoHeader *header) {
    if (header == nullptr)
        return false;
    if (header->magic != BIGOS_BOOT_INFO_V2_MAGIC || header->version != BIGOS_BOOT_INFO_V2_VERSION)
        return false;
    if (header->header_size < sizeof(BootInfoHeader) || header->total_size < header->header_size)
        return false;
    if (header->total_size > BIGOS_BOOT_INFO_V2_MAX_SIZE)
        return false;
    if (!bigos_boot_range_valid(
            header->section_table_offset, header->section_count * sizeof(BootInfoSection), header->total_size))
        return false;
    if (!bigos_boot_aligned(header->section_table_offset, alignof(BootInfoSection)))
        return false;

    const BootInfoSection *sections = (const BootInfoSection *)((const uint8_t *)header + header->section_table_offset);
    bool has_core = false;
    bool has_memory_map = false;
    for (uint16_t i = 0; i < header->section_count; i++) {
        const BootInfoSection &section = sections[i];
        if (!bigos_boot_range_valid(section.offset, section.size, header->total_size))
            return false;
        if (!bigos_boot_aligned(section.offset, section.alignment))
            return false;
        if (section.type == BIGOS_BOOT_SECTION_TYPE_CORE) {
            if ((section.flags & BIGOS_BOOT_SECTION_FLAG_REQUIRED) == 0 || section.size < sizeof(BootInfoCore))
                return false;
            has_core = true;
        } else if (section.type == BIGOS_BOOT_SECTION_TYPE_MEMORY_MAP) {
            if ((section.flags & BIGOS_BOOT_SECTION_FLAG_REQUIRED) == 0 || section.size % sizeof(BootMemoryRegion) != 0)
                return false;
            has_memory_map = true;
        }
    }
    return has_core && has_memory_map;
}

inline const BootInfoSection *bigos_boot_info_v2_find_section(const BootInfoHeader *header, uint32_t type) {
    if (!bigos_boot_info_v2_validate(header))
        return nullptr;
    const BootInfoSection *sections = (const BootInfoSection *)((const uint8_t *)header + header->section_table_offset);
    for (uint16_t i = 0; i < header->section_count; i++) {
        if (sections[i].type == type)
            return &sections[i];
    }
    return nullptr;
}

inline BootHandoff bigos_boot_resolve_handoff(const BootInfoHeader *candidate) {
    if (bigos_boot_info_v2_validate(candidate))
        return {candidate, nullptr};

    const BootInfo *legacy = (const BootInfo *)BIGOS_BOOT_INFO_ADDRESS;
    if (bigos_boot_info_v1_valid(legacy))
        return {nullptr, legacy};

    return {nullptr, nullptr};
}
#endif

#ifdef __cplusplus
static_assert(sizeof(BootInfo) == BIGOS_BOOT_INFO_SIZE);
static_assert(alignof(BootInfo) == BIGOS_BOOT_INFO_ALIGNMENT);
static_assert(__builtin_offsetof(BootInfo, magic) == BIGOS_BOOT_INFO_OFF_MAGIC);
static_assert(__builtin_offsetof(BootInfo, version) == BIGOS_BOOT_INFO_OFF_VERSION);
static_assert(__builtin_offsetof(BootInfo, size) == BIGOS_BOOT_INFO_OFF_SIZE);
static_assert(__builtin_offsetof(BootInfo, flags) == BIGOS_BOOT_INFO_OFF_FLAGS);
static_assert(__builtin_offsetof(BootInfo, handoff_address) == BIGOS_BOOT_INFO_OFF_HANDOFF_ADDRESS);
static_assert(__builtin_offsetof(BootInfo, boot_drive) == BIGOS_BOOT_INFO_OFF_BOOT_DRIVE);
static_assert(__builtin_offsetof(BootInfo, e820_entry_count) == BIGOS_BOOT_INFO_OFF_E820_ENTRY_COUNT);
static_assert(__builtin_offsetof(BootInfo, e820_entry_address) == BIGOS_BOOT_INFO_OFF_E820_ENTRY_ADDRESS);
static_assert(__builtin_offsetof(BootInfo, exfat_data_area_lba) == BIGOS_BOOT_INFO_OFF_EXFAT_DATA_AREA_LBA);
static_assert(__builtin_offsetof(BootInfo, kernel_load_vaddr) == BIGOS_BOOT_INFO_OFF_KERNEL_LOAD_VADDR);
static_assert(__builtin_offsetof(BootInfo, kernel_entry_vaddr) == BIGOS_BOOT_INFO_OFF_KERNEL_ENTRY_VADDR);
static_assert(__builtin_offsetof(BootInfo, kernel_file_size) == BIGOS_BOOT_INFO_OFF_KERNEL_FILE_SIZE);
static_assert(__builtin_offsetof(BootInfo, kernel_memory_size) == BIGOS_BOOT_INFO_OFF_KERNEL_MEMORY_SIZE);
static_assert(sizeof(BootInfoHeader) == 24);
static_assert(alignof(BootInfoHeader) == 4);
static_assert(__builtin_offsetof(BootInfoHeader, magic) == BIGOS_BOOT_INFO_V2_OFF_MAGIC);
static_assert(__builtin_offsetof(BootInfoHeader, version) == BIGOS_BOOT_INFO_V2_OFF_VERSION);
static_assert(__builtin_offsetof(BootInfoHeader, header_size) == BIGOS_BOOT_INFO_V2_OFF_HEADER_SIZE);
static_assert(__builtin_offsetof(BootInfoHeader, total_size) == BIGOS_BOOT_INFO_V2_OFF_TOTAL_SIZE);
static_assert(__builtin_offsetof(BootInfoHeader, flags) == BIGOS_BOOT_INFO_V2_OFF_FLAGS);
static_assert(__builtin_offsetof(BootInfoHeader, boot_protocol) == BIGOS_BOOT_INFO_V2_OFF_BOOT_PROTOCOL);
static_assert(__builtin_offsetof(BootInfoHeader, section_count) == BIGOS_BOOT_INFO_V2_OFF_SECTION_COUNT);
static_assert(__builtin_offsetof(BootInfoHeader, section_table_offset) == BIGOS_BOOT_INFO_V2_OFF_SECTION_TABLE_OFFSET);
static_assert(sizeof(BootInfoSection) == 24);
static_assert(alignof(BootInfoSection) == 4);
static_assert(__builtin_offsetof(BootInfoSection, type) == BIGOS_BOOT_INFO_SECTION_OFF_TYPE);
static_assert(__builtin_offsetof(BootInfoSection, flags) == BIGOS_BOOT_INFO_SECTION_OFF_FLAGS);
static_assert(__builtin_offsetof(BootInfoSection, offset) == BIGOS_BOOT_INFO_SECTION_OFF_OFFSET);
static_assert(__builtin_offsetof(BootInfoSection, size) == BIGOS_BOOT_INFO_SECTION_OFF_SIZE);
static_assert(__builtin_offsetof(BootInfoSection, alignment) == BIGOS_BOOT_INFO_SECTION_OFF_ALIGNMENT);
static_assert(sizeof(BootInfoCore) == 48);
static_assert(alignof(BootInfoCore) == 8);
static_assert(sizeof(BootMemoryRegion) == 40);
static_assert(alignof(BootMemoryRegion) == 8);
static_assert(sizeof(BootStorageMetadata) == 144);
static_assert(alignof(BootStorageMetadata) == 8);
static_assert(sizeof(BootLoaderMetadata) == 176);
static_assert(alignof(BootLoaderMetadata) == 4);
static_assert(__builtin_offsetof(BootMemoryRegion, physical_base) == BIGOS_BOOT_MEMORY_REGION_OFF_PHYSICAL_BASE);
static_assert(__builtin_offsetof(BootMemoryRegion, length) == BIGOS_BOOT_MEMORY_REGION_OFF_LENGTH);
static_assert(__builtin_offsetof(BootMemoryRegion, normalized_type) == BIGOS_BOOT_MEMORY_REGION_OFF_NORMALIZED_TYPE);
static_assert(__builtin_offsetof(BootMemoryRegion, source_type) == BIGOS_BOOT_MEMORY_REGION_OFF_SOURCE_TYPE);
static_assert(__builtin_offsetof(BootMemoryRegion, attributes) == BIGOS_BOOT_MEMORY_REGION_OFF_ATTRIBUTES);
static_assert(__builtin_offsetof(BootMemoryRegion, source_value) == BIGOS_BOOT_MEMORY_REGION_OFF_SOURCE_VALUE);
#endif

#endif   // _ARCH_X86_BOOT_BOOT_INFO_H
