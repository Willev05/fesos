# Copyright (C) 2026 William Lévesque
# SPDX-License-Identifier: GPL-3.0-or-later

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .text : {
        *(.text)
    } :text_rx

    /* Force the VIRTUAL address to the next 2MB, 
       but don't pad the FILE size of the previous segment */
    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata*)
    } :data_ro

    . =ALIGN(0x1000);

    .data : {
        *(.data)
    } :data_rw

    .bss : {
        *(.bss)
        *(COMMON)
    } :data_rw
}

/* Explicitly define Program Headers to ensure only 3 LOAD segments */
PHDRS
{
    text_rx PT_LOAD FLAGS(5); /* Read (4) + Execute (1) */
    data_ro PT_LOAD FLAGS(4);
    data_rw PT_LOAD FLAGS(6); /* Read (4) + Write (2) */
}