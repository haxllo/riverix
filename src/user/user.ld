OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x00400000;

    .text ALIGN(4096) : {
        *(.text*)
    } :text

    .rodata ALIGN(4096) : {
        *(.rodata*)
    } :text

    .data ALIGN(4096) : {
        *(.data*)
    } :data

    .bss ALIGN(4096) : {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
