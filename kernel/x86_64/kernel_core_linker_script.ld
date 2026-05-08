OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .text : {
        *(.text)
        *(.rodata*)
    } :text

    /* Force the VIRTUAL address to the next 2MB, 
       but don't pad the FILE size of the previous segment */
    . = ALIGN(0x200000);

    .data : {
        *(.data)
    } :data

    .bss : {
        *(.bss)
        *(COMMON)
    } :data
}

/* Explicitly define Program Headers to ensure only 2 LOAD segments */
PHDRS
{
    text PT_LOAD FLAGS(5); /* Read (4) + Execute (1) */
    data PT_LOAD FLAGS(6); /* Read (4) + Write (2) */
}