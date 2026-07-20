/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../include/buses/pci.h"
#include "../include/drivers/io.h"

typedef uint32_t (*pci_read_func_t)(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t size);
typedef void (*pci_write_func_t)(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value, uint8_t size);

typedef struct {
    pci_read_func_t read;
    pci_write_func_t write;
} pci_api_t;

static pci_api_t pci_api;

static uint32_t legacy_pci_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t size);
static void legacy_pci_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value, uint8_t size);

/**
 * @brief Initializes the PCI API struct to let drivers read/write to config space.
 */
void pci_init() {
    pci_api.read = legacy_pci_read;
    pci_api.write = legacy_pci_write;
}

/**
 * @brief Gets the header for a specefied device at the Bus/Device/Function requested. 
 * 
 * @param bus
 * @param device
 * @param function
 * @param header A pointer to the header struct to be filled by the function.
 */
void pci_get_header(uint8_t bus, uint8_t device, uint8_t function, pci_header_t *header) {
    //Switch to uint32 pointer in order to simply allow to fill the struct one 32-bit register at a time.
    uint32_t *header_ptr = (uint32_t*)header;

    //Then loop through the first 4 registers which have the data needed for the header.
    for (int i = 0; i < 4; i++) {
        header_ptr[i] = pci_api.read(bus, device, function, i * 0x4, 4);
    }
}

/**
 * @brief Finds the first PCI device with the class and subclass provided.
 * 
 * @param class_code
 * @param subclass
 * @param pci_device The pointer to a pci_device struct to be filled out by the function.
 * 
 * @return Either 1 for success (device found) or 0 for failure (no device found).
 */
int pci_find_device(uint8_t class_code, uint8_t subclass, pci_device_t *pci_device) {
    //We will loop through all possible bus, device, and function to try and find the first device with the requested class and subclass.
    for (uint16_t bus = 0; bus < 256; bus++) { //We need uint16_t to actually get above 255, even if bus itself is represented by 8 bits.
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                pci_header_t header;
                pci_get_header(bus, device, function, &header);

                //If the vendor is all ones, then there is no device at this address.s
                if (header.vendor_id == 0xFFFF) continue;

                //Check to see if we found a device matching the description.
                if (header.class_code == class_code && header.subclass == subclass) {
                    pci_device->bus = (uint8_t)bus;
                    pci_device->device = device;
                    pci_device->function = function;
                    pci_device->device_id = header.device_id;
                    pci_device->vendor_id = header.vendor_id;
                    pci_device->prog_if = header.prog_if;
                    pci_device->class_code = class_code;
                    pci_device->subclass = subclass;

                    //We need to loop through the following 5 registers for the bars.
                    for (int i = 0; i < 6; i++) {
                        pci_device->bars[i] = pci_api.read(bus, device, function, 0x10 + i * 0x4, 4);
                    }
                    return 1;

                }

                //If the device is not multi-function, no need to check the other ones.
                if (function == 0 && (header.header_type & 0x80)) {
                    break;
                }
            }
        }
    }
    return 0;
}

/**
 * @brief Abstracted signature for reading from PCI configuration space.
 * @param pci_device A pointer to the pci device for which to read the configuration space.
 * @param offset Register offset (Up to 4095 for PCIe ECAM).
 * @param size Read width in bytes (1, 2, or 4).
 * @return A value representing the requested read from config space. Can be properly casted down to smaller types if size was < 4.
 */
uint32_t pci_read_config(pci_device_t *pci_device, uint16_t offset, uint8_t size) {
    return pci_api.read(pci_device->bus, pci_device->device, pci_device->function, offset, size);
}

/**
 * @brief Abstracted signature for writing to PCI configuration space.
 * @param pci_device A pointer to the pci device for which to write the configuration space.
 * @param offset Register offset (Up to 4095 for PCIe ECAM).
 * @param value The value to be written. Size is controlled through the size parameter.
 * @param size Read width in bytes (1, 2, or 4).
*/
void pci_write_config(pci_device_t *pci_device, uint16_t offset, uint32_t value, uint8_t size) {
    pci_api.write(pci_device->bus, pci_device->device, pci_device->function, offset, value, size);
}

static uint32_t legacy_pci_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t size) {
    //This is the address structure for a PCI address. The highest bit is enable bit. The rest is self-explanatory. Look at PCI OSDEV for specific info.
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)(device & 0x1F) << 11) | ((uint32_t)(function & 0x7) <<  8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS_PORT_NUMBER, address);

    //Then, after setting the address, we read the data from the data port. We figure the size and internal offset to read from.
    //The offset math comes from which bits in the offset are allowed to modify the internal offset. 
    //For example, byte is allowed to be completely granular, since the offset is measured in bytes. The 3 comes from there, since there are 4 bytes in a dword.
    if (size == 1) return inb(PCI_CONFIG_DATA_PORT_NUMBER + (offset & 0x3));
    if (size == 2) return inw(PCI_CONFIG_DATA_PORT_NUMBER + (offset & 0x2));
    return inl(PCI_CONFIG_DATA_PORT_NUMBER);
}

static void legacy_pci_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value, uint8_t size) {
    //This is the address structure for a PCI address. The highest bit is enable bit. The rest is self-explanatory. Look at PCI OSDEV for specific info.
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)(device & 0x1F) << 11) | ((uint32_t)(function & 0x7) <<  8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS_PORT_NUMBER, address);

    //Then, after setting the address, we read the data from the data port. We figure the size and internal offset to read from.
    //The offset math comes from which bits in the offset are allowed to modify the internal offset. 
    //For example, byte is allowed to be completely granular, since the offset is measured in bytes. The 3 comes from there, since there are 4 bytes in a dword.
    if (size == 1) outb(PCI_CONFIG_DATA_PORT_NUMBER + (offset & 0x3), (uint8_t)value);
    if (size == 2) outw(PCI_CONFIG_DATA_PORT_NUMBER + (offset & 0x2), (uint16_t)value);
    outl(PCI_CONFIG_DATA_PORT_NUMBER, value);
}