#include "../include/common/stdstr.h"

int kultoa(uint64_t number, char *buffer, uint64_t size) {
    uint64_t i = 0;
    if (number == 0) {
        buffer[i++] = 0x30;
        buffer[i] = 0x00;
        return i;
    }

    //Null terminator, so 64-1
    while (i < size - 1 && number > 0){
        buffer[i++] = 0x30 + (number % 10);
        number /= 10;
    }

    buffer[i] = 0x00;
    
    //We reverse the string, since it is reversed for now.
    uint64_t left_ptr = 0;
    uint64_t right_ptr = i - 1;

    while (left_ptr <= right_ptr) {
        if (left_ptr == right_ptr) {
            break;
        }
        char temp = buffer[left_ptr];
        buffer[left_ptr++] = buffer[right_ptr];
        buffer[right_ptr--] = temp;
    }

    return i;
}