OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    kernel PT_LOAD FLAGS(7);
}

SECTIONS {
    . = 0xffffffff80000000;

    .bigos : {
        KEEP (*(.bigos))
    } : kernel

    .init : {
        KEEP (*(SORT_NONE(.init)))
    } : kernel

    .text : {
        *(.text.unlikely .text.*_unlikely .text.unlikely.*)
        *(.text.exit .text.exit.*)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(SORT(.text.sorted.*))
        *(.text .stub .text.* .gnu.linkonce.t.*)
        *(.gnu.warning)
    } : kernel

    .fini : {
        KEEP (*(SORT_NONE(.fini)))
    } : kernel

    . = ALIGN(0x1000);

    .ctors : {
        KEEP (*crtbegin.o(.ctors))
        KEEP (*crtbegin?.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))
    } : kernel

    .dtors : {
        KEEP (*crtbegin.o(.dtors))
        KEEP (*crtbegin?.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*(.dtors))
    } : kernel

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*) 
    } : kernel

    .rodata1 : {
        *(.rodata1) 
    } : kernel

    .eh_frame_hdr : {
        *(.eh_frame_hdr)
        *(.eh_frame_entry 
        .eh_frame_entry.*)
    } : kernel

    .eh_frame : ONLY_IF_RO {
        KEEP (*(.eh_frame)) 
        *(.eh_frame.*) 
    } : kernel

    . = ALIGN(0x1000);

    .data : {
        *(.data .data.* .gnu.linkonce.d.*)
        SORT(CONSTRUCTORS)
    } : kernel

    .4k_area ALIGN(0x1000) : {
        *(.4k_area)
        *(.4k_align_area)
    } : kernel

    .bss : {
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(. != 0 ? 64 / 8 : 1);
    } : kernel

    /DISCARD/ : {
        *(.note.GNU-stack) 
        *(.gnu_debuglink) 
        *(.gnu.lto_*) 
        *(.comment)
    }
}
