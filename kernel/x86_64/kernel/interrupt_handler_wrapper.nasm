; Copyright (C) 2026 William Lévesque
; SPDX-License-Identifier: GPL-3.0-or-later

[BITS 64]
DEFAULT REL

;This is a macro. It will be like a building block for our very similar functions. This simply creates an area with label isr0 forexample, and will push an error code if needed.
;It also pushes the interrupt number from the passed value.
%macro isr_macro 1
    isr%1:
    %if %1 == 8 || %1 == 10 || %1 == 11 || %1 == 12 || %1 == 13 || %1 == 14 || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30
        ;Do nothing, these push a code automatically
    %else 
        push 0
    %endif
    push %1
    jmp interrupt_handler_wrapper
%endmacro

section .text
extern handle_interrupt

interrupt_handler_wrapper:
    ;Push all our CPU registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ;Push the CR2 register as well since certain interrupts need it i.e. page fault. Note: we need to mov to a general purpose register first before push
    mov rax, cr2
    push rax

    ;We want to save rbp for the C handler to have a reference to the beggining of our interrupt frame struct
    mov rdi, rsp

    ;Call our generic C interrupt handler
    call handle_interrupt

    ;Clean up the CR2 from the stack, we wont restore (maybe you can, but i wont in case of side effect)
    add rsp, 8

    ;We restore the registers we pushed
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ;Clean up the stack, where we pushed the interrupt number, and error code
    ;It does not matter if we or the CPU pushed the error code, it needs to be gone
    add rsp, 16

    ;Return to the interrupted code
    iretq

;We create a loop and itterate for each interrupt, passing to the macro which will create 256 very similar code blocks
%assign i 0
%rep 256
    isr_macro i
    %assign i i+1
%endrep

section .data
global interrupt_handler_wrapper_table
;We need this table to make it easy for C to assign the proper interrupt handler wrapper for each interrupt while building the IDT
interrupt_handler_wrapper_table:
    %assign i 0
    %rep 256
        dq isr%+i
        %assign i i+1
    %endrep