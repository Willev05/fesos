/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Some structs were created from information located at: https://wiki.osdev.org/PCI */

#pragma once
#include <stdint.h>

#define PCI_CONFIG_ADDRESS_PORT_NUMBER 0xCF8
#define PCI_CONFIG_DATA_PORT_NUMBER 0xCFC

typedef struct {
    uint16_t vendor_id; //Identifies the manufacturer of the device. Where valid IDs are allocated by PCI-SIG to ensure uniqueness and 0xFFFF is an invalid value that will be returned on read accesses to Configuration Space registers of non-existent devices.
    uint16_t device_id; //Identifies the particular device. Where valid IDs are allocated by the vendor.
    uint16_t command; //Provides control over a device's ability to generate and respond to PCI cycles. Where the only functionality guaranteed to be supported by all devices is, when a 0 is written to this register, the device is disconnected from the PCI bus for all accesses except Configuration Space access.
    uint16_t status; //A register used to record status information for PCI bus related events.
    uint8_t  revision_id; //Specifies a revision identifier for a particular device. Where valid IDs are allocated by the vendor.
    uint8_t  prog_if; //Programming Interface Byte: A read-only register that specifies a register-level programming interface the device has, if it has any at all.
    uint8_t  subclass; //A read-only register that specifies the specific function the device performs.
    uint8_t  class_code; //A read-only register that specifies the type of function the device performs.
    uint8_t  cache_line_size; //Specifies the system cache line size in 32-bit units. A device can limit the number of cacheline sizes it can support, if a unsupported value is written to this field, the device will behave as if a value of 0 was written.
    uint8_t  latency_timer; //Specifies the latency timer in units of PCI bus clocks.
    uint8_t  header_type; // Identifies the layout of the rest of the header beginning at byte 0x10 of the header. If bit 7 of this register is set, the device has multiple functions; otherwise, it is a single function device. 
    /* Types:
        0x0: a general device
        0x1: a PCI-to-PCI bridge
        0x2: a PCI-to-CardBus bridge. */
    uint8_t  bist; //Represents that status and allows control of a devices BIST (built-in self test).
} __attribute__((packed)) pci_header_t;

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    
    uint32_t bars[6];
} pci_device_t;

void pci_init();
void pci_get_header(uint8_t bus, uint8_t device, uint8_t function, pci_header_t *header);
int pci_find_device(uint8_t class_code, uint8_t subclass, pci_device_t *device);
uint32_t pci_read_config(pci_device_t *pci_device, uint16_t offset, uint8_t size);
void pci_write_config(pci_device_t *pci_device, uint16_t offset, uint32_t value, uint8_t size);