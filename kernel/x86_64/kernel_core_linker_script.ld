OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at the High-Half 2GB mark */
    . = 0xFFFFFFFF80000000;

    /* SEGMENT 1: Code and Read-Only Data */
    .text : ALIGN(0x200000) {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(0x200000) {
        *(.rodata .rodata.*)
    } :text

    /* Force a jump to the next 2MB boundary for the Data segment */
    . = ALIGN(0x200000);

    /* SEGMENT 2: Writable Data and BSS */
    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
}

/* Explicitly define Program Headers to ensure only 2 LOAD segments */
PHDRS
{
    text PT_LOAD FLAGS(5); /* Read (4) + Execute (1) */
    data PT_LOAD FLAGS(6); /* Read (4) + Write (2) */
}