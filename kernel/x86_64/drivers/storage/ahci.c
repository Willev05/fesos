//* File: ahci.c */
/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Note: Structure definitions from OSDev Wiki. https://wiki.osdev.org/AHCI */
/* Note: Delays, timeouts, etc from spec document. https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf */
/* Note: For SATA commands. https://tc.gts3.org/cs3210/2016/spring/r/hardware/ATA8-ACS.pdf */
#include "../../include/buses/pci.h"
#include "../../include/memory/kmalloc.h"
#include "../../include/kernel/time.h"
#include "../../include/kernel/errno.h"
#include "../../include/common/printf.h"
#include "../../include/common/stdstr.h"
#include "../../include/drivers/block/lbd.h"

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
}  __attribute__((packed)) HBA_PORT;

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
}  __attribute__((packed)) HBA_MEM;

typedef struct tagHBA_CMD_HEADER
{
	// DW0
	uint8_t  cfl:5;		// Command FIS length in DWORDS, 2 ~ 16
	uint8_t  a:1;		// ATAPI
	uint8_t  w:1;		// Write, 1: H2D, 0: D2H
	uint8_t  p:1;		// Prefetchable

	uint8_t  r:1;		// Reset
	uint8_t  b:1;		// BIST
	uint8_t  c:1;		// Clear busy upon R_OK
	uint8_t  rsv0:1;		// Reserved
	uint8_t  pmp:4;		// Port multiplier port

	uint16_t prdtl;		// Physical region descriptor table length in entries

	// DW1
	volatile
	uint32_t prdbc;		// Physical region descriptor byte count transferred

	// DW2, 3
	uint32_t ctba;		// Command table descriptor base address
	uint32_t ctbau;		// Command table descriptor base address upper 32 bits

	// DW4 - 7
	uint32_t rsv1[4];	// Reserved
}  __attribute__((packed)) HBA_CMD_HEADER;

typedef struct tagHBA_PRDT_ENTRY
{
	uint32_t dba;		// Data base address
	uint32_t dbau;		// Data base address upper 32 bits
	uint32_t rsv0;		// Reserved

	// DW3
	uint32_t dbc:22;		// Byte count, 4M max (0-based)
	uint32_t rsv1:9;		// Reserved
	uint32_t i:1;		// Interrupt on completion
}  __attribute__((packed)) HBA_PRDT_ENTRY;

typedef struct tagHBA_CMD_TBL
{
	// 0x00
	uint8_t  cfis[64];	// Command FIS

	// 0x40
	uint8_t  acmd[16];	// ATAPI command, 12 or 16 bytes

	// 0x50
	uint8_t  rsv[48];	// Reserved

	// 0x80
	HBA_PRDT_ENTRY	prdt_entry[1];	// Physical region descriptor table entries, 0 ~ 65535
}  __attribute__((packed)) HBA_CMD_TBL;

typedef struct tagFIS_REG_H2D
{
	// DWORD 0
	uint8_t  fis_type;	// FIS_TYPE_REG_H2D

	uint8_t  pmport:4;	// Port multiplier
	uint8_t  rsv0:3;		// Reserved
	uint8_t  c:1;		// 1: Command, 0: Control

	uint8_t  command;	// Command register
	uint8_t  featurel;	// Feature register, 7:0
	
	// DWORD 1
	uint8_t  lba0;		// LBA low register, 7:0
	uint8_t  lba1;		// LBA mid register, 15:8
	uint8_t  lba2;		// LBA high register, 23:16
	uint8_t  device;		// Device register

	// DWORD 2
	uint8_t  lba3;		// LBA register, 31:24
	uint8_t  lba4;		// LBA register, 39:32
	uint8_t  lba5;		// LBA register, 47:40
	uint8_t  featureh;	// Feature register, 15:8

	// DWORD 3
	uint8_t  countl;		// Count register, 7:0
	uint8_t  counth;		// Count register, 15:8
	uint8_t  icc;		// Isochronous command completion
	uint8_t  control;	// Control register

	// DWORD 4
	uint8_t  rsv1[4];	// Reserved
}  __attribute__((packed)) FIS_REG_H2D; //20 bytes

typedef struct tagFIS_REG_D2H
{
	// DWORD 0
	uint8_t  fis_type;    // FIS_TYPE_REG_D2H

	uint8_t  pmport:4;    // Port multiplier
	uint8_t  rsv0:2;      // Reserved
	uint8_t  i:1;         // Interrupt bit
	uint8_t  rsv1:1;      // Reserved

	uint8_t  status;      // Status register
	uint8_t  error;       // Error register
	
	// DWORD 1
	uint8_t  lba0;        // LBA low register, 7:0
	uint8_t  lba1;        // LBA mid register, 15:8
	uint8_t  lba2;        // LBA high register, 23:16
	uint8_t  device;      // Device register

	// DWORD 2
	uint8_t  lba3;        // LBA register, 31:24
	uint8_t  lba4;        // LBA register, 39:32
	uint8_t  lba5;        // LBA register, 47:40
	uint8_t  rsv2;        // Reserved

	// DWORD 3
	uint8_t  countl;      // Count register, 7:0
	uint8_t  counth;      // Count register, 15:8
	uint8_t  rsv3[2];     // Reserved

	// DWORD 4
	uint8_t  rsv4[4];     // Reserved
} __attribute__((packed)) FIS_REG_D2H;

typedef volatile struct tagHBA_FIS
{
	// 0x00
	uint8_t	dsfis[28];		// DMA Setup FIS (FIS_DMA_SETUP get struct def from OSDev if needed and replace here)
	uint8_t pad0[4];

	// 0x20
	uint8_t	psfis[20];		// PIO Setup FIS (FIS_PIO_SETUP get struct def from OSDev if needed and replace here)
	uint8_t pad1[12];

	// 0x40
	FIS_REG_D2H	rfis;		// Register – Device to Host FIS
	uint8_t pad2[4];

	// 0x58
	uint8_t	sdbfis[8];		// Set Device Bit FIS (FIS_DEV_BITS get struct def from OSDev if needed and replace here)
	
	// 0x60
	uint8_t ufis[64];

	// 0xA0
	uint8_t rsv[0x100-0xA0];
} __attribute__((packed)) HBA_FIS;

typedef struct {
	HBA_MEM *hba_mmio;
	HBA_PORT *port_mmio;
	dma_block_t control_dma;
	uint8_t port_num;
	uint8_t lba48_support;
	//For the 32 command tables, each with space for up to 248 PRDTs!
	dma_block_t command_tables[32];
} ahci_driver_data_t;

typedef enum
{
	FIS_TYPE_REG_H2D	= 0x27,	// Register FIS - host to device
	FIS_TYPE_REG_D2H	= 0x34,	// Register FIS - device to host
	FIS_TYPE_DMA_ACT	= 0x39,	// DMA activate FIS - device to host
	FIS_TYPE_DMA_SETUP	= 0x41,	// DMA setup FIS - bidirectional
	FIS_TYPE_DATA		= 0x46,	// Data FIS - bidirectional
	FIS_TYPE_BIST		= 0x58,	// BIST activate FIS - bidirectional
	FIS_TYPE_PIO_SETUP	= 0x5F,	// PIO setup FIS - device to host
	FIS_TYPE_DEV_BITS	= 0xA1,	// Set device bits FIS - device to host
} FIS_TYPE;

typedef enum {
	AHCI_PORT_OK,
	AHCI_PORT_TIMEOUT,
	AHCI_PORT_NOT_IMPLEMENTED,
	AHCI_PORT_OUT_OF_MEMORY
} ahci_port_return_t;

static int ahci_read(lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, void *buffer);
static int ahci_write(lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, const void *buffer);
static ahci_port_return_t ahci_init_port(uint8_t port_num, HBA_MEM *hba);
static void ahci_parse_model_string(uint16_t *id_data);

static const lbd_driver_api_t ahci_api = {
	.read = ahci_read,
	.write = ahci_write,
	.flush = NULL
};

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
		kprintf("[AHCI] Port %u: Timed out after sending DMA stop command.\n", port_num);
		return AHCI_PORT_TIMEOUT;
	} 
	kprintf("[AHCI] Port %u: DMA stop success.\n", port_num);

	//Then set bit 4 (FIS Receive Enable) to 0 to disable FIS receive.
	port->cmd &= ~0x10U;

	//And wait again for bit 14.
	start_ms = tsc_timer_get_ms();
	while ((port->cmd & (0x1U << 14)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (port->cmd & (0x1U << 14)) {
		kprintf("[AHCI] Port %u: Timed out after sending FIS Receive disable.\n", port_num);
		return AHCI_PORT_TIMEOUT;
	} 
	kprintf("[AHCI] Port %u: FIS Received disable success.\n", port_num);

	dma_block_t port_mem = kallocate_dma(1);
	if (!port_mem.virtual_addr) {
		kprintf("[AHCI] Port %u: DMA control page allocation failed. Out of memory. Cleaning up and returning...\n", port_num);
	}
	uint64_t dma_physical_base = port_mem.physical_addr;

	//We will take the first 1024 bytes for the command list. (Needs 1KB alignment, satisifed by 4KB page) After, free at 1024 into page
	port->clb = (uint32_t)(dma_physical_base & 0xFFFFFFFFU);
	port->clbu = (uint32_t)(dma_physical_base >> 32);

	//After, we can put the FIS receive base which will take 256 bytes. (Needs 256B alignment, satisifed by 1KB into the page) AFter, free at 1280 into page
	uint64_t fb64 = dma_physical_base + 1024;
	port->fb = (uint32_t)(fb64 & 0xFFFFFFFFU);
	port->fbu = (uint32_t)(fb64 >> 32);

	//Then, renable the FIS receive engine.
	port->cmd |= 0x10U;

	//And wait again for bit 14.
	start_ms = tsc_timer_get_ms();
	while (!(port->cmd & (0x1U << 14)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (!(port->cmd & (0x1U << 14))) {
		kprintf("[AHCI] Port %u: Timed out after sending FIS Receive enable.\n", port_num);
		kfree_dma(port_mem);
		return AHCI_PORT_TIMEOUT;
	} 
	kprintf("[AHCI] Port %u: FIS Received enable success.\n", port_num);

	uint32_t sig = port->sig;
	//Here, we check the port type.
	switch (sig) {
		case 0x00000101:
			kprintf("[AHCI] Port %u: Type: SATA HDD/SSD\n", port_num);
			break;
		case 0xEB140101:
			kprintf("[AHCI] Port %u: Type: ATAPI drive, skipping!\n", port_num);
			kfree_dma(port_mem);
			return AHCI_PORT_NOT_IMPLEMENTED;
			break;
		default:
			kprintf("[AHCI] Port %u: Type: Unsupported (%x), skipping!\n", port_num, sig);
			kfree_dma(port_mem);
			return AHCI_PORT_NOT_IMPLEMENTED;
			break;
	}

	//Reenable the dma processing engine.
	port->cmd |= 0x1U;

	//And wait for bit 15.
	start_ms = tsc_timer_get_ms();
	while (!(port->cmd & (0x1U << 15)) && (tsc_timer_get_ms() - start_ms < 500)) tsc_sleep_ms(1);
	if (!(port->cmd & (0x1U << 15))) {
		kprintf("[AHCI] Port %u: Timed out after sending dma processing engine start.\n", port_num);
		kfree_dma(port_mem);
		return AHCI_PORT_TIMEOUT;
	} 
	kprintf("[AHCI] Port %u: DMA processing engine started success.\n", port_num);

	//Now, we are sure we only have a SATA HDD/SSD.
	//Clear the potential interrupts and/or errors.
	port->serr = 0xffffffffU;
	port->is = 0xffffffffU;

	//Prep a driver_data struct to sotre information like our 32 command list pages.
	ahci_driver_data_t *driver_data = (ahci_driver_data_t*)kmalloc(sizeof(ahci_driver_data_t));
	if (!driver_data) {
		kprintf("[AHCI] Port %u: Driver data struct alloc failed. Out of memory. Cleaning up and returning...\n", port_num);
		kfree_dma(port_mem);
	}

	//Need to allocate 32 dma pages for the command tables and PRDTs.
	//Simply request 32 times, no need for 32 contiguous physical pages. Removes potential issues when trying to allocate during high fragmentation of physical mem.
	for (uint8_t i = 0; i < 32; i++) {
		dma_block_t requested_block = kallocate_dma(1);
		if (!requested_block.virtual_addr) {
			kprintf("[AHCI] Port %u: Unable to allocate a page for one of the command tables! Cleaning up and exiting...\n", port_num);
			kfree_dma(port_mem);
			kfree(driver_data);
			return AHCI_PORT_OUT_OF_MEMORY;
		}
		driver_data->command_tables[i] = requested_block;
	}

	driver_data->hba_mmio = hba;
	driver_data->port_mmio = port;
	driver_data->control_dma = port_mem;

	//For now, initialize the command headers. 
	for (uint8_t i = 0; i < 32; i++) {
		volatile HBA_CMD_HEADER *cmd_header = (volatile HBA_CMD_HEADER*)(port_mem.virtual_addr + sizeof(HBA_CMD_HEADER) * i);

		//Point this header to the page containing out table.
		uint64_t cmdtblp = driver_data->command_tables[i].physical_addr;
		cmd_header->ctba = (uint32_t)(cmdtblp & 0xffffffffU);
		cmd_header->ctbau = (uint32_t)(cmdtblp >> 32);
	}

	//For the initial request, we will use header 0.
	volatile HBA_CMD_HEADER *hdr0 = (volatile HBA_CMD_HEADER*)port_mem.virtual_addr;
	volatile HBA_CMD_TBL *cmdtbl = (volatile HBA_CMD_TBL*)(driver_data->command_tables[0].virtual_addr);
	
	//Now, we write an IDENTIFY DEVICE request to the drive.
	//Wipe the cmd_tbl to get a clean slate.
	volatile_memset(cmdtbl, 0, sizeof(HBA_CMD_TBL));

	//We get the fis and prdt to be written.
	volatile FIS_REG_H2D *fis_h2d = (volatile FIS_REG_H2D*)(&(cmdtbl->cfis));
	volatile HBA_PRDT_ENTRY *prdt = (volatile HBA_PRDT_ENTRY*)(&cmdtbl->prdt_entry);

	//Start by setting up the receiving area. For IDENTIFY DEVICE, 512 bytes are required.
	prdt->dbc = 511; //0 based, so 512 - 1
	//We can put the 512 bytes right after the FIS receive area in the page for now. Put at 1280 which works well with cache lines (64-bit alignment).
	uint64_t dbap = port_mem.physical_addr + 1280;
	prdt->dba = (uint32_t)(dbap & 0xffffffffU);
	prdt->dbau = (uint32_t)(dbap >> 32);
	uint16_t *id_data = (uint16_t*)(port_mem.virtual_addr + 1280);

	//Create the FIS H2D.
	fis_h2d->fis_type = FIS_TYPE_REG_H2D;
	fis_h2d->c = 1;
	fis_h2d->command = 0xEC;

	//Configuyre our header.
	hdr0->cfl = 5;
	hdr0->w = 0;
	hdr0->prdtl = 1;
	hdr0->prdbc = 0; //Reset count

	//Fire the command.
	port->ci |= 1U;

	//Wait for the bit to clear.
	start_ms = tsc_timer_get_ms();
	while ((port->ci & 0x1U) && (tsc_timer_get_ms() - start_ms < 10000)) 
	{
		//Task File Error Check [1]
		if (port->tfd & 0x01) { 
			kprintf("[AHCI] Port %u: Identify Device command rejected by disk status engine!\n", port_num);
			kfree_dma(port_mem);
			kfree(driver_data);
			return AHCI_PORT_TIMEOUT;
    	}
		tsc_sleep_ms(1);
	}
	if (port->ci & 0x1U) {
		kprintf("[AHCI] Port %u: Timed out after sending IDENTIFY DEVICE.\n", port_num);
		kfree_dma(port_mem);
		kfree(driver_data);
		return AHCI_PORT_TIMEOUT;
	} 

	//Need to swap the word's chars due to ATA weirdness.
	ahci_parse_model_string(id_data);
	char *model_string = (char*)(&id_data[27]);
	kprintf("[AHCI] Port %u: Model Number: %s\n", port_num, model_string);

	uint8_t lba48_support;
	uint64_t sector_count;

	//We wanna check 48-bit LBA support.
	if ((id_data[83] & (1 << 10)) && ((id_data[83] & 0xC000) == 0x4000)) { //Seccond check is data integrity check. (Bit 15 = 0, bit 14 = 1)
		//Support. We need to scour potentially 2 different reagions to get the total sector count.
		lba48_support = 1;
		uint32_t sector_count_lba28 = *((uint32_t*)&id_data[60]);
		uint64_t sector_count_lba48 = *((uint64_t*)&id_data[100]);

		//If the larger area contains real meaningful value, then use it.
		if (sector_count_lba48 > 0) sector_count = sector_count_lba48;
		//If not, fall back on the old lba28 size.
		else sector_count = (uint64_t)sector_count_lba28;
	}
	else {
		lba48_support = 0;
		sector_count = (uint64_t)*((uint32_t*)&id_data[60]);
	}

	uint32_t logical_sector_size = 512;
	uint32_t physical_sector_size = 512;
	//Data integrity, if false, assume no support for this and assume 512 sector size.
	if ((id_data[106] & 0xC000) == 0x4000) {
		//Bit 12 represents if the logical sector size > 512 bytes.
		if (id_data[106] & (1 << 12)) {
			logical_sector_size = *((uint32_t*)&id_data[117]) * 2; //*2 since the size is in words originally.
		}

		//Bit 13 represents if there are more than 1 logical sector per physical sector.
		if (id_data[106] & (1 << 13)) {
			//Bits 0-3 hold this info as an exponent (2^n)
			uint8_t exp = id_data[106] & 0xf;
			physical_sector_size = logical_sector_size * (1 << exp);
		}
		else {
			physical_sector_size = logical_sector_size;
		}
	}

	//If write cache, we need to flush after inportant file operations.
	uint8_t write_cache = 0;
	if ((id_data[85] & (1 << 5)) && ((id_data[83] & 0xC000) == 0x4000)) write_cache = 1;

	uint16_t sector_offset = 0;
	//If not equal, then there could be an offset (LBA0 is not the first logical sector of physical block 0).
	if (physical_sector_size != logical_sector_size) {
		if ((id_data[209] & 0xC000) == 0x4000) {
			sector_offset = id_data[209] & 0x3FFF;
		}
	}

	kprintf("[AHCI] Port %u: Logical sector size: %u\n", port_num, logical_sector_size);
	kprintf("[AHCI] Port %u: Physical sector size: %u\n", port_num, physical_sector_size);
	kprintf("[AHCI] Port %u: lba-48bit support: %u\n", port_num, lba48_support);
	kprintf("[AHCI] Port %u: Total number of user addressable sectors: %lu\n", port_num, sector_count);
	kprintf("[AHCI] Port %u: Volatile write cache enabled: %lu\n", port_num, write_cache);
	kprintf("[AHCI] Port %u: Logical alignment offset: %lu\n", port_num, sector_offset);

	//Now, we can prepare a struct to pass to the lbd.
	lbd_logical_drive_t *drive = (lbd_logical_drive_t*)kmalloc(sizeof(lbd_logical_drive_t));

	drive->driver_api = &ahci_api;

	drive->device_info.total_sectors = sector_count;
	drive->device_info.logical_sector_size_bytes = logical_sector_size;
	drive->device_info.physical_sector_size_bytes = physical_sector_size;
	drive->device_info.logical_alignment_offset = sector_offset;
	drive->device_info.max_sectors_per_transfer = 128;
	drive->device_info.flags = LBD_FLAG_W;
	if (write_cache) drive->device_info.flags |= LBD_FLAG_F;

	//Fill the remainder of driver data.
	driver_data->port_num = port_num;
	driver_data->lba48_support = lba48_support;

	drive->driver_data = (void*)driver_data;

	char *raw_bytes = (char*)id_data;
	for (uint8_t i = 0; i < 40; i++) {
		drive->drive_name[i] = raw_bytes[54 + i];
	}

	lbd_register_drive(drive);
	return 0;
}

static int ahci_read(lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, void *buffer) {
	ahci_driver_data_t *driver_data = (ahci_driver_data_t*)(logical_drive->driver_data);
	HBA_MEM *hba = driver_data->hba_mmio;
	HBA_PORT *port = driver_data->port_mmio;
	dma_block_t control_dma = driver_data->control_dma;

	volatile HBA_CMD_HEADER *command_header = (volatile HBA_CMD_HEADER*)control_dma.virtual_addr;
	volatile HBA_CMD_TBL *cmdtbl = (volatile HBA_CMD_TBL*)(control_dma.virtual_addr + 1280);
	volatile FIS_REG_H2D *fis = (volatile FIS_REG_H2D*)(&cmdtbl->cfis);
	volatile HBA_PRDT_ENTRY *prdt = (volatile HBA_PRDT_ENTRY*)(&cmdtbl->prdt_entry);

	//Prepare our header
	command_header->cfl = 5;
	command_header->w = 0;
	command_header->prdtl = 1;
	command_header->prdbc = 0;

	//Prepare the PRDT
	prdt->dbc = (count * logical_drive->device_info.total_sectors);

	//Check to see if we have the lba 48 support.

}

static int ahci_write(lbd_logical_drive_t *logical_drive, uint64_t lba, uint64_t count, const void *buffer) {
	return -EPERM;
}

//Due to ATA returning in word-size registers. This means "AB" stored in a reg to become "BA" due to CPU low-endian.
static void ahci_parse_model_string(uint16_t *id_data) {
	//We need to loop through the 27th to 46th word and swap them.
	for (int w = 27; w <= 46; w++) {
		uint8_t low = (uint8_t)(id_data[w] & 0xff);
		uint8_t high = (uint8_t)(id_data[w] >> 8);
		id_data[w] = (uint16_t)(((uint16_t)low << 8) | high);
	}

	//Replace the last character with 0s since we assume it to just be empty space.
	id_data[46] &= 0xff;
}