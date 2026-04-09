ENTRY(efi_main)

SECTIONS
{
    /* The entry point for UEFI is almost always 4KB in */
    . = 0x1000;

    .text : ALIGN(4096)
    {
        *(.text)
        *(.text.*)
    }

    .data : ALIGN(4096)
    {
        *(.data)
        *(.rodata)
        *(.rodata.*)
    }

    .reloc : ALIGN(4096)
    {
        *(.reloc)
    }
}