#include "../../shared/elf.h"

uint32_t magic_number = 0xDEADC0DE;

int uninitialized_var;

void _start() {
    //Verify the loader's bss and data handling
    if (magic_number == 0xDEADC0DE) {
        uninitialized_var = 1;
    } else {
        uninitialized_var = 2;
    }

    while (1) {
        __asm__("hlt");
    }
}