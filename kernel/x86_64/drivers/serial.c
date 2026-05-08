#include "../include/io.h"

#define COM1 0x3F8

//Initialize the COM1 serial port.
//Help https://docs.freebsd.org/en/articles/serial-uart/
void serial_init() {
    outb(COM1 + 1, 0x00); //Disable interupts
    outb(COM1 + 3, 0x80); //Set DLAB to 1 to set baud rate
    outb(COM1 + 0, 0x03); //Set divisor to 3, achieve 38,400 bps
    outb(COM1 + 1, 0x00); //The upper bits are empty
    outb(COM1 + 3, 0x03); //Set the baud rate by setting bit 7 to 0, setting word size to 8 bits (character), one stop bit, and no parity.
    outb(COM1 + 2, 0xC7); //Sets the FIFO to on, and resets them
}

//Send a single character to the port. 
void serial_putc(char c) {
    //Busy wait while checking the 5th bit, where 1 is there is space in the buffer, 0 is there is not.
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}

void serial_puts(char *s) {
    for (int i = 0; s[i]; i++){
        serial_putc(s[i]);
    }
}