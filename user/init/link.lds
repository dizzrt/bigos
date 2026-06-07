ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
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

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}
