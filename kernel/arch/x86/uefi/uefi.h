#ifndef BIGOS_X86_UEFI_UEFI_H
#define BIGOS_X86_UEFI_UEFI_H

#include <bigos/types.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint64_t UINTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef char16_t CHAR16;

struct EFI_GUID {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct EFI_TABLE_HEADER {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, const CHAR16 *string);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *reset;
    EFI_TEXT_STRING output_string;
};

enum EFI_ALLOCATE_TYPE {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
};

enum EFI_MEMORY_TYPE {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t type;
    uint32_t pad;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl;
    void *restore_tpl;
    EFI_STATUS(EFIAPI *allocate_pages)(EFI_ALLOCATE_TYPE type, EFI_MEMORY_TYPE memory_type, UINTN pages,
                                       EFI_PHYSICAL_ADDRESS *memory);
    EFI_STATUS(EFIAPI *free_pages)(EFI_PHYSICAL_ADDRESS memory, UINTN pages);
    EFI_STATUS(EFIAPI *get_memory_map)(UINTN *memory_map_size, EFI_MEMORY_DESCRIPTOR *memory_map, UINTN *map_key,
                                       UINTN *descriptor_size, uint32_t *descriptor_version);
    EFI_STATUS(EFIAPI *allocate_pool)(EFI_MEMORY_TYPE pool_type, UINTN size, void **buffer);
    EFI_STATUS(EFIAPI *free_pool)(void *buffer);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_STATUS(EFIAPI *handle_protocol)(EFI_HANDLE handle, const EFI_GUID *protocol, void **interface);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    EFI_STATUS(EFIAPI *exit_boot_services)(EFI_HANDLE image_handle, UINTN map_key);
};

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *std_err;
    void *runtime_services;
    EFI_BOOT_SERVICES *boot_services;
};

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    EFI_MEMORY_TYPE image_code_type;
    EFI_MEMORY_TYPE image_data_type;
    EFI_STATUS(EFIAPI *unload)(EFI_HANDLE image_handle);
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_VOLUME_OPEN)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *self, EFI_FILE_PROTOCOL **root);
typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *self, EFI_FILE_PROTOCOL **new_handle, const CHAR16 *file_name,
                                          uint64_t open_mode, uint64_t attributes);
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *self);
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *self, UINTN *buffer_size, void *buffer);
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *self, uint64_t position);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL *self, uint64_t *position);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t revision;
    EFI_VOLUME_OPEN open_volume;
};

struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    void *delete_file;
    EFI_FILE_READ read;
    void *write;
    EFI_FILE_GET_POSITION get_position;
    EFI_FILE_SET_POSITION set_position;
};

#define EFI_SUCCESS 0ull
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ull
#define EFI_LOAD_ERROR 0x8000000000000001ull
#define EFI_NOT_FOUND 0x8000000000000014ull
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001ull
#define EFI_FILE_MODE_READ 0x0000000000000001ull
#define EFI_PAGE_SIZE 4096ull
#define EFI_MEMORY_RUNTIME 0x8000000000000000ull
#define EFI_MEMORY_WB 0x0000000000000008ull
#define EFI_MEMORY_UC 0x0000000000000001ull
#define EFI_MEMORY_WC 0x0000000000000002ull

static constexpr EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static constexpr EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

#endif
