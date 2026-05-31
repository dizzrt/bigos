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

#define DISK_MASTER         0x40
#define DISK_SLAVE          0x50
#define DISK_READ_CMD       0x24
#define SECTORS_PER_CLUSTER 8
#define BYTES_PER_SECTOR    512
#define BOOT_DIR_ADDR       0xf000
#define DATA_AREA_OFF       0x830

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

uint8_t elf_buffer[0x1000];

// functions
extern "C" void boot();
extern "C" void wait_nops();

inline uint8_t inb(uint16_t port) {
    uint8_t ret;

    asm volatile("inb %w1,%b0" : "=a"(ret) : "d"(port));
    return ret;
}

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %b1,%w0" : : "d"(port), "aN"(value));
    return;
}

void print(const char *s) {}

void error(const char *err) {
    print(err);

    while (true) {
        asm volatile("hlt");
    }
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

    // waiting for disk
    while (true) {
        wait_nops();

        uint8_t status = inb(0x01f7) & 0x88;
        if (status == 0x08) {
            break;
        }
    }

    uint16_t data;
    int trf_times = nr_sector * 0x100;
    for (int i = 0; i < trf_times; i++) {
        asm volatile("inw %w1, %w0" : "=a"(data) : "d"(0x01f0));
        buffer[i] = data;
    }

    return 0;
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
    for (; i < 128; i++) {
        FileDirectoryEntry fd_entry = ((FileDirectoryEntry *)entry)[i];
        if (fd_entry.entryType != ET_FDE) {
            continue;
        }

        if (fd_entry.fileAttributes & 0x20 != 0x20) {
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

    return &(entry[i]);
}

void load_kernel() {
    Entry *entry = search_kernel();
    FileDirectoryEntry k_fde = ((FileDirectoryEntry *)entry)[0];
    StreamExtensionDirectoryEntry k_sede = ((StreamExtensionDirectoryEntry *)entry)[1];

    uint32_t offset = (k_sede.firstCluster - 2) * SECTORS_PER_CLUSTER;
    uint64_t ehdr_lba48 = *((uint64_t *)DATA_AREA_OFF) + offset;

    read_disk(DISK_MASTER, 1, ehdr_lba48, (uint16_t *)elf_buffer);
    Elf64_Ehdr *eheader = (Elf64_Ehdr *)elf_buffer;

    // uint32_t pheader_nr = eheader->e_phnum;
    // uint32_t pheader_size = eheader->e_phentsize * pheader_nr;
    // uint16_t nr_sectors = pheader_size / BYTES_PER_SECTOR;
    // if (pheader_size % BYTES_PER_SECTOR != 0) {
    //     nr_sectors++;
    // }

    // kernel should only have one program header which type is LOAD
    uint16_t nr_sectors = 1;
    uint64_t phoff = eheader->e_phoff;
    uint64_t lba48 = ehdr_lba48 + phoff / BYTES_PER_SECTOR;
    read_disk(DISK_MASTER, nr_sectors, lba48, (uint16_t *)elf_buffer);
    Elf64_Phdr *pheader = (Elf64_Phdr *)(elf_buffer + phoff % BYTES_PER_SECTOR);

    int kernel_size = pheader[0].p_memsz;
    int kernel_file_size = pheader[0].p_filesz;
    lba48 = ehdr_lba48 + pheader[0].p_offset / BYTES_PER_SECTOR;

    // save kernel size
    uint32_t *ks_ptr = (uint32_t *)0x80c;
    *ks_ptr = kernel_size;

    setup_paging(kernel_size);
    nr_sectors = (kernel_file_size + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
    read_disk(DISK_MASTER, nr_sectors, lba48, (uint16_t *)KERNEL_VMD);
}

void boot() {
    load_kernel();
}
