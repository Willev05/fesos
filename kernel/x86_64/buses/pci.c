/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../include/buses/pci.h"
#include "../include/drivers/io.h"

/**
 * @brief Reads from the PCI configuration space a register (32-bit) from a requested address/offset.
 * 
 * @param bus
 * @param device
 * @param function
 * @param offset Should be 4-byte (32-bit) alligned i.e. 0x0, 0x4, 0x8, etc.
 * 
 * @return A uint32_t representing the congig from the requested register at the specified address/offset.
 */
uint32_t pci_read_config_long(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    //This is the address structure for a PCI address. The highest bit is enalbe bit. The rest is self-explanatory. Look at PCI OSDEV for specific info.
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)(device & 0x1F) << 11) | ((uint32_t)(function & 0x7) <<  8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS_PORT_NUMBER, address);
    //Then, after setting the address, we read the data from the data port.
    return inl(PCI_CONFIG_DATA_PORT_NUMBER);
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
        header_ptr[i] = pci_read_config_long(bus, device, function, i * 0x4);
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
                        pci_device->bars[i] = pci_read_config_long(bus, device, function, 0x10 + i * 0x4);
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