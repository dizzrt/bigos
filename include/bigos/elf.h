#ifndef _BIG_ELF_H
#define _BIG_ELF_H

#include <bigos/types.h>

// elf64 data types
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

// elf identification, e_ident
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9
#define EI_NIDENT     16

// object file classes, e_ident[EL_CLASS]
#define ELFCLASS32 1   // 32-bit
#define ELFCLASS64 2   // 64-bit

// data encodings, e_ident[EI_DATA]
#define ELFDATA2LSB 1   // little-endian
#define ELFDATA2MSB 2   // big-endian

// operating system and ABI identifiers, e_ident[EI_OSABI]
#define ELFOSABI_SYSV       0     // system v abi
#define ELFOSABI_HPUX       1     // hp-ux operating system
#define ELFOSABI_STANDALONE 255   // standalong(embedded) application

// object file type, e_type
#define ET_NONE   0        // no file type
#define ET_REL    1        // relocatable objec file
#define ET_EXEC   2        // executable file
#define ET_DYN    3        // shared object file
#define ET_CORE   4        // core file
#define ET_LOOS   0xfe00   // environment-specific use
#define ET_HIOS   0xfeff
#define ET_LOPROC 0xff00   // processor-specifi use
#define ET_HIPROC 0xffff

// special section indices
#define SHN_UNDEF  0
#define SHN_LOPROC 0xff00
#define SHN_HIPROC 0xff1f
#define SHN_LOOS   0xff20
#define SHN_HIOS   0xff3f
#define SHN_ABS    0xfff1
#define SHN_COMMON 0xfff2

// section types, sh_type
#define SHT_NULL     0            // Marks an unused section header
#define SHT_PROGBITS 1            // Contains information defined by the program
#define SHT_SYMTAB   2            // Contains a linker symbol table
#define SHT_STRTAB   3            // Contains a string table
#define SHT_RELA     4            // Contains “Rela” type relocation entries
#define SHT_HASH     5            // Contains a symbol hash table
#define SHT_DYNAMIC  6            // Contains dynamic linking tables
#define SHT_NOTE     7            // Contains note information
#define SHT_NOBITS   8            // Contains uninitialized space; does not occupy any space in the file
#define SHT_REL      9            // Contains “Rel” type relocation entries
#define SHT_SHLIB    10           // Reserved
#define SHT_DYNSYM   11           // Contains a dynamic loader symbol table
#define SHT_LOOS     0x60000000   // Environment-specific use
#define SHT_HIOS     0x6FFFFFFF
#define SHT_LOPROC   0x70000000   // Processor-specific use
#define SHT_HIPROC   0x7FFFFFFF

// section attributes, sh_flags
#define SHF_WRITE     0x1          // Section contains writable data
#define SHF_ALLOC     0x2          // Section is allocated in memory image of program
#define SHF_EXECINSTR 0x4          // Section contains executable instructions
#define SHF_MASKOS    0x0F000000   // Environment-specific use
#define SHF_MASKPROC  0xF0000000   // Processor-specific use

// symbol bindings
#define STB_LOCAL  0    // Not visible outside the object file
#define STB_GLOBAL 1    // Global symbol, visible to all object files
#define STB_WEAK   2    // Global scope, but with lower precedence than global symbols
#define STB_LOOS   10   // Environment-specific use
#define STB_HIOS   12
#define STB_LOPROC 13   // Processor-specific use
#define STB_HIPROC 15

// symbol types
#define STT_NOTYPE  0    // No type specified (e.g., an absolute symbol)
#define STT_OBJECT  1    // Data object
#define STT_FUNC    2    // Function entry point
#define STT_SECTION 3    // Symbol is associated with a section
#define STT_FILE    4    // Source file associated with the object file
#define STT_LOOS    10   // Environment-specific use
#define STT_HIOS    12
#define STT_LOPROC  13   // Processor-specific use
#define STT_HIPROC  15

// segment types, p_type
#define PT_NULL    0            // Unused entry
#define PT_LOAD    1            // Loadable segment
#define PT_DYNAMIC 2            // Dynamic linking tables
#define PT_INTERP  3            // Program interpreter path name
#define PT_NOTE    4            // Note sections
#define PT_SHLIB   5            // Reserved
#define PT_PHDR    6            // Program header table
#define PT_LOOS    0x60000000   // Environment-specific use
#define PT_HIOS    0x6FFFFFFF
#define PT_LOPROC  0x70000000   // Processor-specific use
#define PT_HIPROC  0x7FFFFFFF

// segment attributes, p_flags
#define PF_X        0x1          // Execute permission
#define PF_W        0x2          // Write permission
#define PF_R        0x4          // Read permission
#define PF_MASKOS   0x00FF0000   // These flag bits are reserved for environment-specific use
#define PF_MASKPROC 0xFF000000   // These flag bits are reserved for processor-specific use

// dynamic table entries
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_LOOS         0x60000000
#define DT_HIOS         0x6FFFFFFF
#define DT_LOPROC       0x70000000
#define DT_HIPROC       0x7FFFFFFF

struct Elf64_Ehdr {
    uint8_t e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;   // entry point address
    Elf64_Off e_phoff;    // start of program headers
    Elf64_Off e_shoff;    // start of section headers
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;      // size of this header (64 bytes)
    Elf64_Half e_phentsize;   // size of program header
    Elf64_Half e_phnum;       // number of program headers
    Elf64_Half e_shentsize;   // size of section header
    Elf64_Half e_shnum;       // number of section headers
    Elf64_Half e_shstrndx;    // section header string table index
};

struct Elf64_Shdr {
    Elf64_Word sh_name;         // section name
    Elf64_Word sh_type;         // section type
    Elf64_Xword sh_flags;       // section attributes
    Elf64_Addr sh_addr;         // virtual address in memory
    Elf64_Off sh_offset;        // offset in file
    Elf64_Xword sh_size;        // size of this section
    Elf64_Word sh_link;         // link to other section
    Elf64_Word sh_info;         // miscellaneous information
    Elf64_Xword sh_addralign;   // address alignment boundary
    Elf64_Xword sh_entsize;     // size of entries, if section has table
};

struct Elf64_Phdr {
    Elf64_Word p_type;      // type of segment
    Elf64_Word p_flags;     // segment attributes
    Elf64_Off p_offset;     // offset in file
    Elf64_Addr p_vaddr;     // virtual address in memory
    Elf64_Addr p_paddr;     // reserved
    Elf64_Xword p_filesz;   // size of segment in file
    Elf64_Xword p_memsz;    // size of segment in memory
    Elf64_Xword p_align;    // alignment of segment
};

struct Elf64_Sym {
    Elf64_Word st_name;    // symbol name
    uint8_t st_info;       // type and binding attributes
    uint8_t st_other;      // reserved
    Elf64_Half st_shndx;   // section table index
    Elf64_Addr st_value;   // symbol value
    Elf64_Xword st_size;   // size of object
};

struct Elf64_Rel {
    Elf64_Addr r_offset;   // address of reference
    Elf64_Xword r_info;    // symbol index and type of relocation
};

struct Elf64_Rela {
    Elf64_Addr r_offset;     // address of reference
    Elf64_Xword r_info;      // symbol index and type of relocation
    Elf64_Sxword r_addend;   // constant part of expression
};

struct Elf64_Dyn {
    Elf64_Sxword d_tag;
    union d_un {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    };
};

#endif   // _BIG_ELF_H
