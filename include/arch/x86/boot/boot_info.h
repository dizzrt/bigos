#ifndef _ARCH_X86_BOOT_BOOT_INFO_H
#define _ARCH_X86_BOOT_BOOT_INFO_H

#include <bigos/types.h>

#define BIGOS_BOOT_INFO_MAGIC     0x42494f53u
#define BIGOS_BOOT_INFO_VERSION   1u
#define BIGOS_BOOT_INFO_ADDRESS   0x840u
#define BIGOS_BOOT_INFO_SIZE      72u
#define BIGOS_BOOT_INFO_ALIGNMENT 8u

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
#endif

#endif   // _ARCH_X86_BOOT_BOOT_INFO_H
