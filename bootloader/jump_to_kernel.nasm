; Copyright (C) 2026 William Lévesque
; SPDX-License-Identifier: GPL-3.0-or-later

[BITS 64]
DEFAULT REL

section .text
global jump_to_kernel

; C function signature: 
; void jump_to_kernel(
;    uint64_t pml4_phys,       // RDI
;    uint64_t entry_point,     // RSI
;    uint64_t stack_top,       // RDX
;    void* boot_info           // RCX
; );

jump_to_kernel:
    ;Disable interupts
    cli

    ;Prepare support for our NX bit
    ;We need to save the rcx and rdx registers since this will overwrite/use them
    mov r8, rcx ;boot_info
    mov r9, rdx ;stack_top

    ;Do the actual NX toggle
    mov ecx, 0xC0000080 ;MSR number of the specific EFER register
    rdmsr
    or eax, (1 << 11)
    wrmsr

    ;We need to setup a temporary GDT
    lgdt [gdt64_ptr]

    ;Switch to new PML4 table
    mov cr3, rdi

    ;Reload the data segments
    mov ax, 0x10
    mov ds, ax
    mov ss, ax

    ;Setup new kernel stack
    mov rsp, r9
    mov rbp, rsp

    ;Get the boot info struct ready for our kernel, using the System V ABI, so RDI needs the pointer
    mov rdi, r8

    ;Prepare for long jump
    push 0x08
    push rsi

    ;JUMP!
    retfq

    ;Should never reach
    hlt

align 8
gdt64:
    ;Null Descriptor
    dq 0x0000000000000000 

    ;Kernel Code Segment (Selector 0x08)
    ;Bits: Present(1), Ring 0(00), S-type(1), Exec(1), Read(1), LongMode(1)
    dq 0x00af9a000000ffff 

    ;Kernel Data Segment (Selector 0x10)
    ;Bits: Present(1), Ring 0(00), S-type(1), Write(1), Read(1)
    dq 0x00cf92000000ffff

gdt64_ptr:
    dw $ - gdt64 - 1    ; Limit (Size of GDT minus 1)
    dq gdt64            ; Base Address (Pointer to the table)
