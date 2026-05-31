#include <arch/x86/boot/boot_info.h>
#include <bigos/elf.h>
#include <bigos/types.h>

#ifndef __GNUC__
#warning GCC is recommended for building
#endif

#define KERNEL_PDT_VMD 0x5000
#define KERNEL_PTS_PMD 0x100000
#define KERNEL_PTS_VMD 0x100000
#define KERNEL_PMD     0x1000000
#define KERNEL_VMD     0xffffffff80000000
#define PAGE_SIZE      0x1000

#define DISK_MASTER          0x40
#define DISK_SLAVE           0x50
#define DISK_READ_CMD        0x24
#define SECTORS_PER_CLUSTER  8
#define BYTES_PER_SECTOR     512
#define BOOT_DIR_ADDR        0xf000
#define DATA_AREA_OFF        0x830
#define BOOT_DRIVE_ADDR      0x802
#define E820_COUNT_ADDR      0x800
#define E820_ENTRIES_ADDR    0x500
#define KERNEL_SIZE_ADDR     0x80c
#define VGA_TEXT_ADDR        0xb8000
#define VGA_TEXT_COLS        80
#define VGA_TEXT_ROWS        25
#define ATA_STATUS_BSY       0x80
#define ATA_STATUS_DRQ       0x08
#define ATA_STATUS_DF        0x20
#define ATA_STATUS_ERR       0x01
#define ATA_POLL_LIMIT       1000000
#define PHDR_BUFFER_SIZE     0x4000
#define EM_X86_64            62
#define EV_CURRENT           1
#define SUPPORTED_BOOT_DRIVE 0x80

// entry type
#define ET_FDE  0x85   // file directory entry
#define ET_SEDE 0xc0   // stream extension directory entry
#define ET_FNDE 0xc1   // file name directory entry

struct Entry {
    uint8_t reserved[32];
};

struct FileDirectoryEntry {
    uint8_t entryType;
    uint8_t secondaryCount;
    uint16_t setChecksum;
    uint16_t fileAttributes;
    uint16_t reserved1;
    uint32_t createTimeStamp;
    uint32_t lastModifiedTimeStamp;
    uint32_t lastAccessedTimeStamp;
    uint8_t Create10msIncrement;
    uint8_t lastModified10msIncrement;
    uint8_t createUtcOffset;
    uint8_t lastModifiedUtcOffset;
    uint8_t LastAccessedUtcOffset;
    uint8_t reserved2[7];
};

struct StreamExtensionDirectoryEntry {
    uint8_t entryType;
    uint8_t generalSecondaryFlags;
    uint8_t reserved1;
    uint8_t nameLength;
    uint16_t nameHash;
    uint16_t reserved2;
    uint64_t validDataLength;
    uint32_t reserved3;
    uint32_t firstCluster;
    uint64_t dataLength;
};

struct fileNameDirectoryEntry {
    uint8_t entryType;
    uint8_t generalSecondaryFlags;
    // const char fileName[30];
    const char16_t fileName[15];
};

uint8_t disk_buffer[BYTES_PER_SECTOR];
uint8_t phdr_buffer[PHDR_BUFFER_SIZE];
uint16_t vga_cursor = 0;
uint64_t g_kernel_entry = KERNEL_VMD;

// functions
extern "C" uint64_t boot();
extern "C" void wait_nops();

inline uint8_t inb(uint16_t port) {
    uint8_t ret;

    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    return;
}

void print(const char *s) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_TEXT_ADDR;
    while (*s != 0) {
        char ch = *s++;
        if (ch == '\n') {
            vga_cursor = ((vga_cursor / VGA_TEXT_COLS) + 1) * VGA_TEXT_COLS;
            continue;
        }
        if (vga_cursor >= VGA_TEXT_COLS * VGA_TEXT_ROWS) {
            vga_cursor = 0;
        }
        vga[vga_cursor++] = (uint16_t)ch | 0x0f00;
    }
}

void error(const char *err) {
    print(err);

    while (true) {
        asm volatile("hlt");
    }
}

void *memset(void *dest, int value, uint64_t size) {
    uint8_t *out = (uint8_t *)dest;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = (uint8_t)value;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, uint64_t size) {
    uint8_t *out = (uint8_t *)dest;
    const uint8_t *in = (const uint8_t *)src;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
    return dest;
}

int wait_for_ata_data() {
    for (uint32_t retry = 0; retry < ATA_POLL_LIMIT; retry++) {
        wait_nops();
        uint8_t status = inb(0x01f7);
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0) {
            return -2;
        }
        if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == ATA_STATUS_DRQ) {
            return 0;
        }
    }
    return -1;
}

int read_disk(uint8_t disk, uint16_t nr_sector, uint64_t lba48, uint16_t *buffer) {
    uint8_t LBA_1 = lba48 & 0xff;
    uint8_t LBA_2 = (lba48 & 0xff00) >> 0x08;
    uint8_t LBA_3 = (lba48 & 0xff0000) >> 0x10;
    uint8_t LBA_4 = (lba48 & 0xff000000) >> 0x18;
    uint8_t LBA_5 = (lba48 & 0xff00000000) >> 0x20;
    uint8_t LBA_6 = (lba48 & 0xff0000000000) >> 0x28;

    outb(0x01f6, disk);
    outb(0x01f2, (uint8_t)(nr_sector >> 8));
    outb(0x01f3, LBA_4);
    outb(0x01f4, LBA_5);
    outb(0x01f5, LBA_6);
    outb(0x01f2, (uint8_t)nr_sector);
    outb(0x01f3, LBA_1);
    outb(0x01f4, LBA_2);
    outb(0x01f5, LBA_3);
    outb(0x01f7, DISK_READ_CMD);

    uint16_t data;
    for (uint16_t sector = 0; sector < nr_sector; sector++) {
        int wait_status = wait_for_ata_data();
        if (wait_status != 0) {
            return wait_status;
        }

        for (int i = 0; i < 0x100; i++) {
            asm volatile("inw %1, %0" : "=a"(data) : "Nd"((uint16_t)0x01f0));
            buffer[sector * 0x100 + i] = data;
        }
    }

    return 0;
}

void checked_read_disk(uint16_t nr_sector, uint64_t lba48, uint16_t *buffer) {
    int status = read_disk(DISK_MASTER, nr_sector, lba48, buffer);
    if (status == -1) {
        error("disk timeout");
    }
    if (status == -2) {
        error("disk controller error");
    }
    if (status != 0) {
        error("disk read error");
    }
}

void setup_paging(int size) {
    int nr_pt = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    int nr_pd = (nr_pt + 512 - 1) / 512;

    uint64_t phys_addr = KERNEL_PTS_PMD;
    uint64_t entry_attrbute = 0x0000000000000003;
    uint64_t *entry_ptr = (uint64_t *)KERNEL_PDT_VMD;
    for (int i = 0; i < nr_pd; i++) {
        entry_ptr[i] = phys_addr | entry_attrbute;
        phys_addr += 0x1000;
    }

    phys_addr = KERNEL_PMD;
    entry_ptr = (uint64_t *)KERNEL_PTS_VMD;
    for (int i = 0; i < nr_pt; i++) {
        entry_ptr[i] = phys_addr | entry_attrbute;
        phys_addr += 0x1000;
    }
}

Entry *search_kernel() {
    Entry *entry = (Entry *)BOOT_DIR_ADDR;
    // const char *kernel_name = "k e r n e l";
    const char16_t *kernel_name = u"kernel";

    int i = 0;
    for (; i < 126; i++) {
        FileDirectoryEntry fd_entry = ((FileDirectoryEntry *)entry)[i];
        if (fd_entry.entryType != ET_FDE) {
            continue;
        }

        if ((fd_entry.fileAttributes & 0x20) != 0x20) {
            // not a file
            continue;
        }

        StreamExtensionDirectoryEntry sed_entry = ((StreamExtensionDirectoryEntry *)entry)[i + 1];
        if (sed_entry.entryType != ET_SEDE) {
            continue;
        }

        if (sed_entry.nameLength != 6) {
            continue;
        }

        fileNameDirectoryEntry fnd_entry = ((fileNameDirectoryEntry *)entry)[i + 2];
        if (fnd_entry.entryType != ET_FNDE) {
            continue;
        }

        bool isKernel = true;
        for (int j = 0; j < 6; j++) {
            if (fnd_entry.fileName[j] != kernel_name[j]) {
                isKernel = false;
                break;
            }
        }

        if (isKernel) {
            break;
        }
    }

    if (i >= 126) {
        return nullptr;
    }

    return &(entry[i]);
}

bool validate_elf_header(const Elf64_Ehdr *eheader) {
    return eheader->e_ident[EI_MAG0] == 0x7f && eheader->e_ident[EI_MAG1] == 'E' && eheader->e_ident[EI_MAG2] == 'L' &&
           eheader->e_ident[EI_MAG3] == 'F' && eheader->e_ident[EI_CLASS] == ELFCLASS64 &&
           eheader->e_ident[EI_DATA] == ELFDATA2LSB && eheader->e_ident[EI_VERSION] == EV_CURRENT &&
           eheader->e_type == ET_EXEC && eheader->e_machine == EM_X86_64 && eheader->e_ehsize == sizeof(Elf64_Ehdr) &&
           eheader->e_phentsize == sizeof(Elf64_Phdr) && eheader->e_phoff >= sizeof(Elf64_Ehdr) && eheader->e_phnum > 0;
}

bool validate_segment_target(const Elf64_Phdr *pheader) {
    if (pheader->p_filesz > pheader->p_memsz) {
        return false;
    }
    if (pheader->p_vaddr < KERNEL_VMD) {
        return false;
    }
    uint64_t start = pheader->p_vaddr - KERNEL_VMD;
    uint64_t end = start + pheader->p_memsz;
    if (end < start) {
        return false;
    }
    return end <= 0x4000000;
}

bool validate_segment_file_range(const Elf64_Phdr *pheader, uint64_t file_size) {
    uint64_t file_end = pheader->p_offset + pheader->p_filesz;
    return file_end >= pheader->p_offset && file_end <= file_size;
}

void read_file_bytes(uint64_t file_lba, uint64_t file_offset, uint64_t size, uint8_t *dest) {
    uint64_t copied = 0;
    while (copied < size) {
        uint64_t absolute_offset = file_offset + copied;
        uint64_t lba = file_lba + absolute_offset / BYTES_PER_SECTOR;
        uint64_t sector_offset = absolute_offset % BYTES_PER_SECTOR;
        uint64_t to_copy = BYTES_PER_SECTOR - sector_offset;
        if (to_copy > size - copied) {
            to_copy = size - copied;
        }

        checked_read_disk(1, lba, (uint16_t *)disk_buffer);
        memcpy(dest + copied, disk_buffer + sector_offset, to_copy);
        copied += to_copy;
    }
}

uint64_t load_kernel() {
    if (*(uint8_t *)BOOT_DRIVE_ADDR != SUPPORTED_BOOT_DRIVE) {
        error("unsupported boot drive");
    }

    Entry *entry = search_kernel();
    if (entry == nullptr) {
        error("kernel not found");
    }

    StreamExtensionDirectoryEntry k_sede = ((StreamExtensionDirectoryEntry *)entry)[1];

    uint32_t offset = (k_sede.firstCluster - 2) * SECTORS_PER_CLUSTER;
    uint64_t ehdr_lba48 = *((uint64_t *)DATA_AREA_OFF) + offset;

    checked_read_disk(1, ehdr_lba48, (uint16_t *)disk_buffer);
    Elf64_Ehdr *eheader = (Elf64_Ehdr *)disk_buffer;
    if (!validate_elf_header(eheader)) {
        error("invalid kernel");
    }

    uint64_t phdr_bytes = (uint64_t)eheader->e_phentsize * eheader->e_phnum;
    if (phdr_bytes > PHDR_BUFFER_SIZE || eheader->e_phoff + phdr_bytes < eheader->e_phoff ||
        eheader->e_phoff + phdr_bytes > k_sede.dataLength) {
        error("invalid kernel phdr");
    }

    uint64_t entry_point = eheader->e_entry;
    read_file_bytes(ehdr_lba48, eheader->e_phoff, phdr_bytes, phdr_buffer);
    Elf64_Phdr *pheaders = (Elf64_Phdr *)phdr_buffer;

    uint64_t kernel_memory_size = 0;
    uint64_t kernel_file_size = 0;
    bool entry_in_load_segment = false;
    bool has_load_segment = false;
    for (uint16_t i = 0; i < eheader->e_phnum; i++) {
        Elf64_Phdr *pheader = &pheaders[i];
        if (pheader->p_type != PT_LOAD) {
            continue;
        }
        has_load_segment = true;
        if (!validate_segment_target(pheader) || !validate_segment_file_range(pheader, k_sede.dataLength)) {
            error("invalid kernel segment");
        }

        uint64_t segment_end = pheader->p_vaddr - KERNEL_VMD + pheader->p_memsz;
        if (segment_end > kernel_memory_size) {
            kernel_memory_size = segment_end;
        }
        uint64_t file_end = pheader->p_offset + pheader->p_filesz;
        if (file_end > kernel_file_size) {
            kernel_file_size = file_end;
        }
        if (entry_point >= pheader->p_vaddr && entry_point < pheader->p_vaddr + pheader->p_memsz) {
            entry_in_load_segment = true;
        }
    }

    if (!has_load_segment || !entry_in_load_segment) {
        error("invalid kernel entry");
    }

    setup_paging(kernel_memory_size);

    for (uint16_t i = 0; i < eheader->e_phnum; i++) {
        Elf64_Phdr *pheader = &pheaders[i];
        if (pheader->p_type != PT_LOAD) {
            continue;
        }

        uint8_t *target = (uint8_t *)pheader->p_vaddr;
        read_file_bytes(ehdr_lba48, pheader->p_offset, pheader->p_filesz, target);
        if (pheader->p_memsz > pheader->p_filesz) {
            memset(target + pheader->p_filesz, 0, pheader->p_memsz - pheader->p_filesz);
        }
    }

    // Preserve legacy handoff aliases while BootInfo consumers migrate.
    uint32_t *ks_ptr = (uint32_t *)KERNEL_SIZE_ADDR;
    *ks_ptr = (uint32_t)kernel_memory_size;

    BootInfo *boot_info = (BootInfo *)BIGOS_BOOT_INFO_ADDRESS;
    boot_info->magic = BIGOS_BOOT_INFO_MAGIC;
    boot_info->version = BIGOS_BOOT_INFO_VERSION;
    boot_info->size = sizeof(BootInfo);
    boot_info->flags = 0;
    boot_info->handoff_address = BIGOS_BOOT_INFO_ADDRESS;
    boot_info->boot_drive = *(uint8_t *)BOOT_DRIVE_ADDR;
    boot_info->e820_entry_count = *(uint16_t *)E820_COUNT_ADDR;
    boot_info->e820_entry_address = E820_ENTRIES_ADDR;
    boot_info->exfat_data_area_lba = *(uint64_t *)DATA_AREA_OFF;
    boot_info->kernel_load_vaddr = KERNEL_VMD;
    boot_info->kernel_entry_vaddr = entry_point;
    boot_info->kernel_file_size = kernel_file_size;
    boot_info->kernel_memory_size = kernel_memory_size;

    g_kernel_entry = entry_point;
    return entry_point;
}

uint64_t boot() {
    return load_kernel();
}
