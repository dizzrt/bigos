/* Shared linker script for BigOS user-space C programs.
 *
 * Produces a static ET_EXEC ELF64 image loaded by the kernel ELF loader at a
 * fixed user base. Link with -nostdlib -static -z max-page-size=0x1000. crt0
 * provides the _start entry; main is the user program entry. The kernel loader
 * maps PT_LOAD segments by their own vaddr, so .text/.rodata/.data/.bss are
 * placed in separate page-aligned, separately-permissioned segments. */
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);    /* R + X */
    rodata PT_LOAD FLAGS(4);  /* R */
    data PT_LOAD FLAGS(6);    /* R + W (also covers .bss) */
}

SECTIONS
{
    . = 0x0000000000400000;
    .text : ALIGN(0x1000)
    {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata : ALIGN(0x1000)
    {
        *(.rodata*)
    } :rodata

    . = ALIGN(0x1000);
    .data : ALIGN(0x1000)
    {
        *(.data*)
    } :data

    .bss : ALIGN(0x10)
    {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}
