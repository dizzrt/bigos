OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0xffffffff80000000;

    .bigos : {
        KEEP (*(.bigos))
    } : text

    .init : {
        KEEP (*(SORT_NONE(.init)))
    } : text

    .text : {
        *(.text.unlikely .text.*_unlikely .text.unlikely.*)
        *(.text.exit .text.exit.*)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(SORT(.text.sorted.*))
        *(.text .stub .text.* .gnu.linkonce.t.*)
        *(.gnu.warning)
    } : text

    .fini : {
        KEEP (*(SORT_NONE(.fini)))
    } : text

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*) 
    } : rodata

    .rodata1 : {
        *(.rodata1) 
    } : rodata

    .eh_frame_hdr : {
        *(.eh_frame_hdr)
        *(.eh_frame_entry 
        .eh_frame_entry.*)
    } : rodata

    .eh_frame : ONLY_IF_RO {
        KEEP (*(.eh_frame)) 
        *(.eh_frame.*) 
    } : rodata

    . = ALIGN(0x1000);

    .eh_frame : ONLY_IF_RW {
        KEEP (*(.eh_frame)) 
        *(.eh_frame.*) 
    } : data

    .ctors : {
        KEEP (*crtbegin.o(.ctors))
        KEEP (*crtbegin?.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))
    } : data

    .dtors : {
        KEEP (*crtbegin.o(.dtors))
        KEEP (*crtbegin?.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*(.dtors))
    } : data

    .data : {
        *(.data .data.* .gnu.linkonce.d.*)
        SORT(CONSTRUCTORS)
    } : data

    .4k_area ALIGN(0x1000) : {
        *(.4k_area)
        *(.4k_align_area)
    } : data

    .bss : {
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(. != 0 ? 64 / 8 : 1);
    } : data

    /DISCARD/ : {
        *(.note.GNU-stack) 
        *(.gnu_debuglink) 
        *(.gnu.lto_*) 
        *(.comment)
    }
}
