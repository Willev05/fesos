; Copyright (C) 2026 William Lévesque
; SPDX-License-Identifier: GPL-3.0-or-later

[BITS 64]
DEFAULT REL

section .text
global load_gdt

load_gdt:
    ;Load our gdt
    lgdt [rdi]

    ;Reload the data segments
    mov ax, 0x10
    mov ds, ax
    mov ss, ax

    push 0x08
    lea rax, [rel ret_to_c]
    push rax
    retfq 

ret_to_c:
    ret