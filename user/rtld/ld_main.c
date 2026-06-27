/* BigOS user-space dynamic linker (ld.so) core.
 *
 * Freestanding, position-independent, default-off bounded dynamic linker.
 * Built ET_DYN; entered by the kernel at _dl_start (ld_start.s) with the System
 * V initial stack carrying argc/argv/envp plus a bounded auxv. It:
 *   1. self-relocates (only R_X86_64_RELATIVE), using AT_BASE for its own bias;
 *   2. parses the main image PT_DYNAMIC via AT_PHDR/AT_PHNUM;
 *   3. loads a bounded number of DT_NEEDED objects under /lib into the bounded
 *      shared-object region with the SYS_DYN_MAP/SYS_DYN_PROTECT primitives;
 *   4. applies an eager (BIND_NOW) relocation subset
 *      (R_X86_64_RELATIVE/GLOB_DAT/JMP_SLOT/64) with global-scope symbol
 *      binding (main image first, then load order);
 *   5. returns the main image real entry (AT_ENTRY) to _dl_start, which jumps to
 *      it on the untouched initial stack.
 *
 * No host runtime, no exceptions/RTTI, no C++ global constructors. Only raw
 * int 0x80 syscalls. Anything outside the bounded subset (TLS, IFUNC, symbol
 * versions, unsupported reloc types, over-limit counts) is a deterministic
 * failure: emit a diagnostic marker and SYS_EXIT, never jump to an unrelocated
 * or undefined address. All globals are PC-relative (-fPIC -fvisibility=hidden
 * -fno-plt) so they need no relocation before self-relocation completes. */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long int64_t;
typedef unsigned long size_t;

/* Syscall numbers (mirror user/libc/include/sys_nr.h). */
#define SYS_DEBUG_WRITE 0
#define SYS_WRITE       2
#define SYS_EXIT        3
#define SYS_OPEN        5
#define SYS_READ        6
#define SYS_CLOSE       7
#define SYS_LSEEK       20
#define SYS_DYN_MAP     59
#define SYS_DYN_PROTECT 60

#define O_RDONLY 0
#define SEEK_SET 0

/* VmaPermission bits (mirror bigos::proc::VmaPermission). */
#define PERM_R 1
#define PERM_W 2
#define PERM_X 4

/* Bounded shared-object layout (mirror include/bigos/proc.h). */
#define USER_DYN_LIB_BASE 0x0000000000a00000ul
#define USER_DYN_LIB_END  0x0000000001000000ul
#define PAGE_SIZE         0x1000ul
#define IO_CHUNK          512ul

/* auxv a_type values. */
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_ENTRY  9

/* ELF program header types / dynamic tags / reloc types we use. */
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_PHDR    6

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_SYMENT   11
#define DT_JMPREL   23

#define R_X86_64_64       1
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JMP_SLOT 7
#define R_X86_64_RELATIVE 8

#define SHN_UNDEF  0
#define STB_WEAK   2

#define ELF64_R_SYM(i)  ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffUL)
#define ELF64_ST_BIND(i) ((i) >> 4)

#define LD_MAX_OBJECTS 8   /* main image + bounded DT_NEEDED libraries */
#define LD_MAX_NEEDED  4

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Phdr;

typedef struct {
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} EhdrTail;   /* fields after the 16-byte e_ident */

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Rela;

typedef struct {
    uint64_t base;        /* load bias */
    const char *strtab;
    const Sym *symtab;
    uint32_t symcount;    /* from DT_HASH nchain */
} Object;

/* ---- raw syscalls ---- */
static long sys1(long n, long a0) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a0) : "rcx", "r11", "memory");
    return r;
}
static long sys3(long n, long a0, long a1, long a2) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return r;
}

static void ld_write(const char *s, size_t n) { (void)sys3(SYS_WRITE, 1, (long)s, (long)n); }

static size_t ld_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

static void ld_fail(const char *reason) {
    const char *prefix = "BIGOS_DYNLINK_FAILED ldso-";
    ld_write(prefix, ld_strlen(prefix));
    ld_write(reason, ld_strlen(reason));
    ld_write("\n", 1);
    (void)sys1(SYS_EXIT, 127);
    for (;;) {}
}

static int ld_streq(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != 0 && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

static void ld_memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (uint8_t)c;
}

static uint64_t page_down(uint64_t v) { return v & ~(PAGE_SIZE - 1); }
static uint64_t page_up(uint64_t v) { return (v + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }

/* ---- self-relocation (R_X86_64_RELATIVE only) ----
 * Walks our own _DYNAMIC (run-time address from _dl_start), finds DT_RELA, and
 * applies only relative relocations using our load bias. MUST run before any
 * access to a global that itself needs relocation. */
static void self_relocate(uint64_t base, const Dyn *dyn) {
    const Rela *rela = 0;
    uint64_t relasz = 0;
    uint64_t relaent = sizeof(Rela);
    for (const Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_RELA)
            rela = (const Rela *)(base + d->d_val);
        else if (d->d_tag == DT_RELASZ)
            relasz = d->d_val;
        else if (d->d_tag == DT_RELAENT)
            relaent = d->d_val;
    }
    if (rela == 0 || relaent == 0)
        return;
    uint64_t count = relasz / relaent;
    for (uint64_t i = 0; i < count; i++) {
        const Rela *r = (const Rela *)((const uint8_t *)rela + i * relaent);
        if (ELF64_R_TYPE(r->r_info) == R_X86_64_RELATIVE)
            *(uint64_t *)(base + r->r_offset) = base + (uint64_t)r->r_addend;
    }
}

/* ---- bounded file read into a user buffer (<= IO_CHUNK per syscall) ---- */
static int ld_pread(int fd, uint64_t off, void *dst, uint64_t len) {
    if (sys3(SYS_LSEEK, fd, (long)off, SEEK_SET) < 0)
        return -1;
    uint8_t *p = (uint8_t *)dst;
    uint64_t done = 0;
    while (done < len) {
        uint64_t want = len - done;
        if (want > IO_CHUNK)
            want = IO_CHUNK;
        long got = sys3(SYS_READ, fd, (long)(p + done), (long)want);
        if (got <= 0)
            return -1;
        done += (uint64_t)got;
    }
    return 0;
}

/* Reads the ELF header + program headers of an open object into caller buffers.
 * Returns the program-header count, or 0 on failure. */
static uint16_t read_ehdr_phdrs(int fd, EhdrTail *eh_out, Phdr *ph_out, uint16_t ph_max) {
    uint8_t ident[16];
    if (ld_pread(fd, 0, ident, 16) != 0)
        return 0;
    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F' || ident[4] != 2)
        return 0;
    if (ld_pread(fd, 16, eh_out, sizeof(EhdrTail)) != 0)
        return 0;
    if (eh_out->e_phentsize != sizeof(Phdr) || eh_out->e_phnum == 0 || eh_out->e_phnum > ph_max)
        return 0;
    if (ld_pread(fd, eh_out->e_phoff, ph_out, (uint64_t)eh_out->e_phnum * sizeof(Phdr)) != 0)
        return 0;
    return eh_out->e_phnum;
}

/* Maps one PT_LOAD of an object at load_base + page_down(p_vaddr): reserve R|W
 * demand-zero pages, touch them so they fault in writable, load the file bytes,
 * then drop to the final permissions. Returns 0 on success. */
static int map_segment(int fd, uint64_t load_base, const Phdr *ph) {
    uint64_t va = load_base + ph->p_vaddr;
    uint64_t map_base = page_down(va);
    uint64_t map_end = page_up(load_base + ph->p_vaddr + ph->p_memsz);
    uint64_t span = map_end - map_base;
    if (sys3(SYS_DYN_MAP, (long)map_base, (long)span, PERM_R | PERM_W) != (long)map_base)
        return -1;
    /* Touch every page so it materializes present + writable before read/copy. */
    ld_memset((void *)map_base, 0, span);
    if (ph->p_filesz != 0 && ld_pread(fd, ph->p_offset, (void *)va, ph->p_filesz) != 0)
        return -1;
    /* Final permissions from segment flags: PF_X(1)->exec, PF_W(2)->write. */
    long perm = PERM_R;
    if (ph->p_flags & 1)
        perm |= PERM_X;
    if (ph->p_flags & 2)
        perm |= PERM_W;
    if ((perm & (PERM_W | PERM_X)) == (PERM_W | PERM_X))
        perm = PERM_R | PERM_W;   /* never publish W+X */
    if (sys3(SYS_DYN_PROTECT, (long)map_base, (long)span, perm) != 0)
        return -1;
    return 0;
}

/* Parses an object's _DYNAMIC (at load_base + dyn_vaddr) into an Object record
 * (strtab/symtab/symcount) and reports its DT_NEEDED string offsets and reloc
 * tables. Returns 0 on success. */
static int parse_dynamic(uint64_t load_base, uint64_t dyn_vaddr, Object *obj, uint32_t *needed_out,
    uint32_t *needed_count, const Rela **rela_out, uint64_t *relasz_out, const Rela **jmprel_out,
    uint64_t *pltrelsz_out) {
    const Dyn *dyn = (const Dyn *)(load_base + dyn_vaddr);
    const char *strtab = 0;
    const Sym *symtab = 0;
    const uint32_t *hash = 0;
    const Rela *rela = 0;
    const Rela *jmprel = 0;
    uint64_t relasz = 0;
    uint64_t pltrelsz = 0;
    uint32_t needed_n = 0;
    for (const Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_STRTAB: strtab = (const char *)(load_base + d->d_val); break;
            case DT_SYMTAB: symtab = (const Sym *)(load_base + d->d_val); break;
            case DT_HASH: hash = (const uint32_t *)(load_base + d->d_val); break;
            case DT_RELA: rela = (const Rela *)(load_base + d->d_val); break;
            case DT_RELASZ: relasz = d->d_val; break;
            case DT_JMPREL: jmprel = (const Rela *)(load_base + d->d_val); break;
            case DT_PLTRELSZ: pltrelsz = d->d_val; break;
            case DT_NEEDED:
                if (needed_out != 0 && needed_n < LD_MAX_NEEDED)
                    needed_out[needed_n] = (uint32_t)d->d_val;
                needed_n++;
                break;
            default: break;
        }
    }
    if (strtab == 0 || symtab == 0 || hash == 0)
        return -1;
    if (needed_n > LD_MAX_NEEDED)
        return -1;
    obj->base = load_base;
    obj->strtab = strtab;
    obj->symtab = symtab;
    obj->symcount = hash[1];   /* nchain == symbol count */
    if (needed_count != 0)
        *needed_count = needed_n;
    if (rela_out != 0)
        *rela_out = rela;
    if (relasz_out != 0)
        *relasz_out = relasz;
    if (jmprel_out != 0)
        *jmprel_out = jmprel;
    if (pltrelsz_out != 0)
        *pltrelsz_out = pltrelsz;
    return 0;
}

/* Global-scope symbol resolution: scan objects in load order (main image first)
 * for a defined (st_shndx != SHN_UNDEF) symbol named `name`. Returns its
 * absolute address, or 0 when undefined. */
static uint64_t resolve(const Object *objs, uint32_t nobj, const char *name) {
    for (uint32_t o = 0; o < nobj; o++) {
        const Object *obj = &objs[o];
        for (uint32_t i = 0; i < obj->symcount; i++) {
            const Sym *s = &obj->symtab[i];
            if (s->st_shndx == SHN_UNDEF || s->st_name == 0)
                continue;
            if (ld_streq(obj->strtab + s->st_name, name))
                return obj->base + s->st_value;
        }
    }
    return 0;
}

/* Applies one relocation table to `obj`, resolving symbols against the global
 * scope. Unsupported types or unresolved non-weak symbols are fatal. */
static void apply_relocs(
    const Rela *rela, uint64_t bytes, const Object *obj, const Object *objs, uint32_t nobj) {
    if (rela == 0)
        return;
    uint64_t count = bytes / sizeof(Rela);
    for (uint64_t i = 0; i < count; i++) {
        const Rela *r = &rela[i];
        uint64_t type = ELF64_R_TYPE(r->r_info);
        uint64_t symidx = ELF64_R_SYM(r->r_info);
        uint64_t *where = (uint64_t *)(obj->base + r->r_offset);
        if (type == R_X86_64_RELATIVE) {
            *where = obj->base + (uint64_t)r->r_addend;
            continue;
        }
        if (type != R_X86_64_64 && type != R_X86_64_GLOB_DAT && type != R_X86_64_JMP_SLOT)
            ld_fail("reloc-type");
        const Sym *s = &obj->symtab[symidx];
        const char *name = obj->strtab + s->st_name;
        uint64_t value = resolve(objs, nobj, name);
        if (value == 0 && s->st_shndx != SHN_UNDEF)
            value = obj->base + s->st_value;
        if (value == 0) {
            if (ELF64_ST_BIND(s->st_info) == STB_WEAK)
                value = 0;   /* deterministic weak-undefined binding */
            else
                ld_fail("undef-sym");
        }
        if (type == R_X86_64_64)
            *where = value + (uint64_t)r->r_addend;
        else
            *where = value;
    }
}

/* Entry from ld_start.s: __initial_sp points at argc; __dynamic is the run-time
 * address of our own _DYNAMIC. Returns the main image real entry to jump to. */
uint64_t _dl_main(uint64_t *__initial_sp, const Dyn *__dynamic) {
    /* Walk the initial stack to the auxv (after argv NULL and envp NULL). */
    uint64_t argc = __initial_sp[0];
    uint64_t *cursor = &__initial_sp[1 + argc + 1];   /* skip argc, argv[], NULL */
    while (*cursor != 0)                              /* skip envp[] */
        cursor++;
    cursor++;                                          /* skip envp NULL */
    uint64_t *auxv = cursor;

    uint64_t at_base = 0, at_phdr = 0, at_phnum = 0, at_entry = 0;
    for (uint64_t *a = auxv; a[0] != AT_NULL; a += 2) {
        switch (a[0]) {
            case AT_BASE: at_base = a[1]; break;
            case AT_PHDR: at_phdr = a[1]; break;
            case AT_PHNUM: at_phnum = a[1]; break;
            case AT_ENTRY: at_entry = a[1]; break;
            default: break;
        }
    }

    /* Self-relocate first using our own load bias (AT_BASE). */
    self_relocate(at_base, __dynamic);

    if (at_phdr == 0 || at_phnum == 0 || at_entry == 0)
        ld_fail("auxv");

    /* Locate the main image load base and PT_DYNAMIC from its phdrs. PIE images
     * carry PT_PHDR, so base = AT_PHDR - PT_PHDR.p_vaddr. */
    const Phdr *mphdr = (const Phdr *)at_phdr;
    uint64_t main_base = 0;
    uint64_t main_dyn_vaddr = 0;
    int have_base = 0;
    for (uint64_t i = 0; i < at_phnum; i++) {
        if (mphdr[i].p_type == PT_PHDR) {
            main_base = at_phdr - mphdr[i].p_vaddr;
            have_base = 1;
        }
    }
    if (!have_base)
        ld_fail("no-phdr");
    for (uint64_t i = 0; i < at_phnum; i++) {
        if (mphdr[i].p_type == PT_DYNAMIC)
            main_dyn_vaddr = mphdr[i].p_vaddr;
    }
    if (main_dyn_vaddr == 0)
        ld_fail("no-dynamic");

    Object objs[LD_MAX_OBJECTS];
    uint32_t nobj = 0;
    uint32_t main_needed[LD_MAX_NEEDED];
    uint32_t main_needed_count = 0;
    const Rela *main_rela = 0, *main_jmprel = 0;
    uint64_t main_relasz = 0, main_pltrelsz = 0;
    if (parse_dynamic(main_base, main_dyn_vaddr, &objs[0], main_needed, &main_needed_count, &main_rela,
            &main_relasz, &main_jmprel, &main_pltrelsz) != 0)
        ld_fail("main-dynamic");
    nobj = 1;

    /* Load each DT_NEEDED object under /lib into the shared-object region. */
    uint64_t lib_next = USER_DYN_LIB_BASE;
    const Rela *lib_rela[LD_MAX_OBJECTS];
    uint64_t lib_relasz[LD_MAX_OBJECTS];
    const Rela *lib_jmprel[LD_MAX_OBJECTS];
    uint64_t lib_pltrelsz[LD_MAX_OBJECTS];
    for (uint32_t n = 0; n < main_needed_count; n++) {
        if (nobj >= LD_MAX_OBJECTS)
            ld_fail("too-many-objs");
        char path[128];
        const char *dir = "/lib/";
        const char *name = objs[0].strtab + main_needed[n];
        size_t pi = 0;
        for (const char *p = dir; *p != 0 && pi < sizeof(path) - 1; p++)
            path[pi++] = *p;
        for (const char *p = name; *p != 0 && pi < sizeof(path) - 1; p++)
            path[pi++] = *p;
        path[pi] = 0;

        int fd = (int)sys3(SYS_OPEN, (long)path, O_RDONLY, 0);
        if (fd < 0)
            ld_fail("needed-open");

        EhdrTail eh;
        Phdr ph[8];
        uint16_t phnum = read_ehdr_phdrs(fd, &eh, ph, 8);
        if (phnum == 0)
            ld_fail("needed-elf");

        uint64_t lib_base = lib_next;
        uint64_t lib_dyn_vaddr = 0;
        uint64_t lib_high = 0;
        for (uint16_t i = 0; i < phnum; i++) {
            if (ph[i].p_type == PT_LOAD) {
                if (map_segment(fd, lib_base, &ph[i]) != 0)
                    ld_fail("needed-map");
                uint64_t end = page_up(lib_base + ph[i].p_vaddr + ph[i].p_memsz);
                if (end > lib_high)
                    lib_high = end;
            } else if (ph[i].p_type == PT_DYNAMIC) {
                lib_dyn_vaddr = ph[i].p_vaddr;
            }
        }
        (void)sys1(SYS_CLOSE, fd);
        if (lib_dyn_vaddr == 0 || lib_high == 0 || lib_high > USER_DYN_LIB_END)
            ld_fail("needed-layout");
        lib_next = page_up(lib_high);

        if (parse_dynamic(lib_base, lib_dyn_vaddr, &objs[nobj], 0, 0, &lib_rela[nobj], &lib_relasz[nobj],
                &lib_jmprel[nobj], &lib_pltrelsz[nobj]) != 0)
            ld_fail("needed-dynamic");
        nobj++;
    }

    /* Eager (BIND_NOW) relocation of every object against the global scope. */
    apply_relocs(main_rela, main_relasz, &objs[0], objs, nobj);
    apply_relocs(main_jmprel, main_pltrelsz, &objs[0], objs, nobj);
    for (uint32_t o = 1; o < nobj; o++) {
        apply_relocs(lib_rela[o], lib_relasz[o], &objs[o], objs, nobj);
        apply_relocs(lib_jmprel[o], lib_pltrelsz[o], &objs[o], objs, nobj);
    }

    return at_entry;
}
