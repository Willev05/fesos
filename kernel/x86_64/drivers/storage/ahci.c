//* File: ahci.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Note: Structure definitions derived from OSDev Wiki. https://wiki.osdev.org/AHCI */
/* Note: Delays, timeouts, etc from spec document. https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf */
#include "../../include/buses/pci.h"
#include "../../include/memory/kmalloc.h"
#include "../../include/kernel/time.h"
#include "../../include/kernel/errno.h"
#include "../../include/common/printf.h"

typedef volatile struct tagHBA_PORT
{
	uint32_t clb;		// 0x00, command list base address, 1K-byte aligned
	uint32_t clbu;		// 0x04, command list base address upper 32 bits
	uint32_t fb;		// 0x08, FIS base address, 256-byte aligned
	uint32_t fbu;		// 0x0C, FIS base address upper 32 bits
	uint32_t is;		// 0x10, interrupt status
	uint32_t ie;		// 0x14, interrupt enable
	uint32_t cmd;		// 0x18, command and status
	uint32_t rsv0;		// 0x1C, Reserved
	uint32_t tfd;		// 0x20, task file data
	uint32_t sig;		// 0x24, signature
	uint32_t ssts;		// 0x28, SATA status (SCR0:SStatus)
	uint32_t sctl;		// 0x2C, SATA control (SCR2:SControl)
	uint32_t serr;		// 0x30, SATA error (SCR1:SError)
	uint32_t sact;		// 0x34, SATA active (SCR3:SActive)
	uint32_t ci;		// 0x38, command issue
	uint32_t sntf;		// 0x3C, SATA notification (SCR4:SNotification)
	uint32_t fbs;		// 0x40, FIS-based switch control
	uint32_t rsv1[11];	// 0x44 ~ 0x6F, Reserved
	uint32_t vendor[4];	// 0x70 ~ 0x7F, vendor specific
} __attribute((packed)) HBA_PORT;

typedef volatile struct tagHBA_MEM
{
	// 0x00 - 0x2B, Generic Host Control
	uint32_t cap;		// 0x00, Host capability
	uint32_t ghc;		// 0x04, Global host control
	uint32_t is;		// 0x08, Interrupt status
	uint32_t pi;		// 0x0C, Port implemented
	uint32_t vs;		// 0x10, Version
	uint32_t ccc_ctl;	// 0x14, Command completion coalescing control
	uint32_t ccc_pts;	// 0x18, Command completion coalescing ports
	uint32_t em_loc;		// 0x1C, Enclosure management location
	uint32_t em_ctl;		// 0x20, Enclosure management control
	uint32_t cap2;		// 0x24, Host capabilities extended
	uint32_t bohc;		// 0x28, BIOS/OS handoff control and status

	// 0x2C - 0x9F, Reserved
	uint8_t  rsv[0xA0-0x2C];

	// 0xA0 - 0xFF, Vendor specific registers
	uint8_t  vendor[0x100-0xA0];

	// 0x100 - 0x10FF, Port control registers
	HBA_PORT	ports[32];	// 1 ~ 32
} __attribute((packed)) HBA_MEM;

typedef enum {
	AHCI_PORT_OK,
	AHCI_PORT_TIMEOUT,
	AHCI_PORT_NOT_IMPLEMENTED
} ahci_port_return_t;

static int ahci_read(void *driver_data, uint64_t lba, uint64_t count, void *buffer);
static int ahci_write(void *driver_data, uint64_t lba, uint64_t count, const void *buffer);
static ahci_port_return_t ahci_init_port(uint8_t port_num, HBA_MEM *hba);

int ahci_init_device(pci_device_t *pci_device) {
	kprintf("[AHCI] Found controller at bus %lu, device %lu, function %lu.\n", pci_device->bus, pci_device->device, pci_device->function);

	//Start with looking at its power state.
	//Requires us to find it within the capability list.
	//Start by checking if said capability list exists.
	uint16_t status_reg = pci_read_config(pci_device, 0x6, 2);
	//If bit 4 is set, then the list exists!
	if (status_reg & 0x10) {
		kprintf("[AHCI] Controller supports capability list. Exploring for potential power management.\n");
		//We will start to browse the linked-list of capability pointers. 
		uint8_t next_offset = pci_read_config(pci_device, 0x34, 1);
		while (next_offset != 0) {
			uint8_t current_offset = next_offset;
			uint16_t cap = pci_read_config(pci_device, current_offset, 2);

			uint8_t cap_id = cap & 0xFF;
			next_offset = (uint8_t)(cap >> 8);

			kprintf("[AHCI] Found capability %x at offset %x.\n", cap_id, current_offset);
			if (cap_id == 0x1) {
				//Found the power capability
				kprintf("[AHCI] Controller supports power management. Attempting to wake.\n");
				uint16_t pmcsr = pci_read_config(pci_device, current_offset + 0x4, 2);
				kprintf("[AHCI] Controller power state: %u.\n", pmcsr & 0x3);
				pmcsr &= ~0x3U;
				pci_write_config(pci_device, current_offset + 0x4, pmcsr, 2);
				break;
			}
		}
	}

    //We need to get the BAR5 address.
    uint64_t bar_address = pci_device->bars[5] & ~0xF;

    //Turn on bus mastering and memory space in the PCI command register.
    uint16_t pci_command_register = (uint16_t)pci_read_config(pci_device, 0x4, 2);
    pci_command_register |= 0x6;
    pci_write_config(pci_device, 0x4, pci_command_register, 2);

    //We then map this to MMIO area. 
    HBA_MEM *hba = kmap_mmio(bar_address, sizeof(HBA_MEM), MMIO_DEFAULT);

	//Now we request ownership of the AHCI controller from the BIOS, if it is owned by it.
	if (hba->bohc & 1) {
		hba->bohc |= (1 << 1);

		while (hba->bohc & 1);
	}

	//We reset the controller.
	hba->ghc |= 1;

	uint64_t start_ms = tsc_timer_get_ms();

	while ((hba->ghc & 1) && (tsc_timer_get_ms() - start_ms < 1000)) tsc_sleep_ms(1);
	if (hba->ghc & 1) {
		kprintf("[AHCI] Controller at bus %lu, device %lu, function %lu has timed out after sending reset command.\n", pci_device->bus, pci_device->device, pci_device->function);
		return -ETIMEDOUT;
	} 

	//Then, we enable AHCI mode.
	hba->ghc |= (1 << 31);

	//We then also wait again to ensure HBA can probe the ports and update PI properly after reset.
	tsc_sleep_ms(20);

	//Now, its time to read the implemented and connected ports.
	//Outside loop will look for the implemented ports (physically wired) and the inner one will check if they are connected.
	for (uint8_t i = 0; i < 32; i++) {
		if (hba->pi & (1 << i)) {
			HBA_PORT *port = &hba->ports[i];

			//Get the sata status register PxSSTS.
			uint32_t ssts = port->ssts;

			//We then want to grab the device detection (DET) and interface power management (IPM).
			uint8_t det = ssts & 0xF;
			uint8_t ipm = (ssts >> 8) & 0xF;

			//If the device is detected and communicating (det == 0x3) AND the interface is in an active state (ipm == 0x1) then the port is active and connected.
			if (det == 3 && ipm == 1) {
				kprintf("[AHCI] Found drive on Port %lu.\n", (uint64_t)i);
				ahci_init_port(i, hba);
			}
		}
	}
}

static ahci_port_return_t ahci_init_port(uint8_t port_num, HBA_MEM *hba) {
	HBA_PORT *port = &hba->ports[port_num];

	//Even though we reset the controller, we will still stop verify and stop the DMA engine.

	//We start by setting bit 0 (Start) to 0 sgutting down the DMA engine.
	port->cmd &= ~0x1U;

	//And wait to see if success shut down (bit 15).
	uint64_t start_ms = tsc_timer_get_ms();
	while ((port->cmd & (0x1U << 15)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (port->cmd & (0x1U << 15)) {
		kprintf("[AHCI] Port %u has timed out after sending DMA stop command.\n", port_num);
		return AHCI_PORT_TIMEOUT;
	} 

	//Then set bit 4 (FIS Receive Enable) to 0 to disable FIS receive.
	port->cmd &= ~0x10U;

	//And wait again for bit 14.
	start_ms = tsc_timer_get_ms();
	while ((port->cmd & (0x1U << 14)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (port->cmd & (0x1U << 14)) {
		kprintf("[AHCI] Port %u has timed out after sending FIS Receive disable.\n", port_num);
		return AHCI_PORT_TIMEOUT;
	} 

	dma_block_t port_mem = kallocate_dma(1);
	uint64_t dma_physical_base = port_mem.physical_addr;

	//We will take the first 1024 bytes for the command list. (Needs 1KB alignment, satisifed by 4KB page)
	port->clb = (uint32_t)(dma_physical_base & 0xFFFFFFFF);
	port->clbu = (uint32_t)(dma_physical_base >> 32);

	//After, we can put the FIS receive base which will take 256 bytes. (Needs 256B alignment, satisifed by 1KB into the page)
	uint64_t fb64 = dma_physical_base + 1024;
	port->fb = (uint32_t)(fb64 & 0xFFFFFFFF);
	port->fbu = (uint32_t)(fb64 >> 32);

	//Then, renable the FIS receive engine.
	port->cmd |= 0x10U;

	//And wait again for bit 14.
	start_ms = tsc_timer_get_ms();
	while (!(port->cmd & (0x1U << 14)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (port->cmd & !(0x1U << 14)) {
		kprintf("[AHCI] Port %u has timed out after sending FIS Receive enable.\n", port_num);
		return AHCI_PORT_TIMEOUT;
	} 

	uint32_t sig = port->sig;
	//Here, we check the port type.
	switch (sig) {
		case 0x00000101:
			kprintf("[AHCI] Port %u: Type: SATA HDD/SSD\n", port_num);
			break;
		case 0xEB140101:
			kprintf("[AHCI] Port %u: Type: ATAPI drive, skipping!\n", port_num);
			return AHCI_PORT_NOT_IMPLEMENTED;
			break;
		default:
			kprintf("[AHCI] Port %u: Type: Unsupported (%x), skipping!\n", port_num, sig);
			return AHCI_PORT_NOT_IMPLEMENTED;
			break;
	}

	//Now, we are sure we only have a SATA HDD/SSD.
	//Clear the potential interrupts and/or errors.
}