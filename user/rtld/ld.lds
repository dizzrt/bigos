/* Linker script for the BigOS user-space dynamic linker (ld.so).
 *
 * Produces a position-independent ET_DYN with entry _dl_start. Link with
 * -shared -fPIC so internal references self-resolve to R_X86_64_RELATIVE and
 * the image carries a PT_DYNAMIC the loader self-relocates against. The kernel
 * maps it at USER_INTERP_BASE; ld.so derives its own bias from auxv AT_BASE. */
ENTRY(_dl_start)

PHDRS
{
    text PT_LOAD FLAGS(5);      /* R + X */
    data PT_LOAD FLAGS(6);      /* R + W (also covers .bss) */
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = 0;
    .text : ALIGN(0x1000)
    {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata : ALIGN(0x1000)
    {
        *(.rodata*)
    } :data
    .data.rel.ro : { *(.data.rel.ro*) } :data
    .dynamic : { *(.dynamic) } :data :dynamic
    .got : { *(.got) *(.got.plt) } :data
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
        *(.gnu.hash)
    }
}
