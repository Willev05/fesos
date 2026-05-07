[BITS 64]

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

    ;Switch to new PML4 table
    mov cr3, rdi

    ;Setup new kernel stack
    mov rsp, rdx
    mov rbp, rsp

    ;Get the boot info struct ready for our kernel, using the System V ABI, so RDI needs the pointer
    mov rdi, rcx

    ;The jump to kernel
    jmp rsi

    ;Should never reach
    hlt
