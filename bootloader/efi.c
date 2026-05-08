#include "efi.h"
#include "../shared/elf.h"
#include "../shared/boot_info.h"
#include "../shared/memory.h"

uint16_t *EFIAPI to_string(uint64_t input);
uint16_t *EFIAPI to_string_hex(uint64_t input);
void check_EFI_error(EFI_STATUS status, uint16_t *error_message, EFI_SYSTEM_TABLE *SystemTable);
EFI_MEMORY_DESCRIPTOR *EFIAPI get_memory_map(UINTN *map_size, UINTN *descriptor_size, UINTN *map_key, EFI_SYSTEM_TABLE *SystemTable);

extern void __attribute__((sysv_abi)) jump_to_kernel(uint64_t pml4_phys, uint64_t entry_point, uint64_t stack_top, void *BootInfo);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        L"EFI image loaded!\r\n"
    );

    EFI_STATUS status = 0;

    //Allocate a page for outr boot struct which we will be filling up.
    boot_info *BootInfo;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS *)&BootInfo);
    check_EFI_error(status, L"Could not allocate a page for the boot struct!", SystemTable);

    //Get the linear pixel buffer address.
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface = NULL;
    status = SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, (void**)&gop_interface);
    check_EFI_error(status, L"Unable to get the GOP!", SystemTable);
    SystemTable->conout->OutputString(SystemTable->conout, L"Got GOP!\r\n");

    //Fill the slots for the boot info struct.
    BootInfo->framebuffer_base = (uint64_t)gop_interface->Mode->FrameBufferBase;
    BootInfo->framebuffer_size = (uint64_t)gop_interface->Mode->FrameBufferSize;
    BootInfo->horizontal_resolution = gop_interface->Mode->Info->HorizontalResolution;
    BootInfo->vertical_resolution = gop_interface->Mode->Info->VerticalResolution;
    BootInfo->pixels_per_scan_line = gop_interface->Mode->Info->PixelsPerScanLine;

    //Get a file handle on the drive root.
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system;
    EFI_FILE_PROTOCOL *root;

    EFI_GUID loaded_image_protocol_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    status = SystemTable->BootServices->HandleProtocol(ImageHandle, &loaded_image_protocol_guid, (void**)&loaded_image);
    check_EFI_error(status, L"Unable to get loaded image protocol!", SystemTable);
    

    //We do this to get the file system protocol for the specific drive this image was loaded from.
    EFI_GUID file_system_protocol_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    status = SystemTable->BootServices->HandleProtocol(loaded_image->DeviceHandle, &file_system_protocol_guid, (void**)&file_system);
    check_EFI_error(status, L"Unable to get file system protocol!", SystemTable);

    //We get the root of the drive from the file system.
    status = file_system->OpenVolume(file_system, &root);
    check_EFI_error(status, L"Unable to get root directory file protocol!", SystemTable);

    //Now, we open the kernel core file, which sits on the root.
    EFI_FILE_PROTOCOL *kernel_file;
    status = root->Open(root, &kernel_file, L"kernel_core.elf", 1, 0);
    check_EFI_error(status, L"Unable to find or open kernel file!", SystemTable);

    //Get the file size
    UINTN info_size = 0;
    EFI_FILE_INFO *file_info;
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

    //We need to get the size of the struct, since the text is variable length.
    kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, NULL);

    //With the size, we request the memory, and load the file info.
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (void **)&file_info);
    check_EFI_error(status, L"Unable to allocate memory for file info!", SystemTable);
    status = kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, file_info);
    check_EFI_error(status, L"Unable to execute getinfo for the kernel file!", SystemTable);

    //We allocate the space needed to load the entire file.
    UINTN kernel_elf_required_pages = (file_info->FileSize + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS kernel_elf_buffer;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, kernel_elf_required_pages, &kernel_elf_buffer);
    check_EFI_error(status, L"Unable to allocate memory to store kernel elf file!", SystemTable);

    //Load the entire thing.
    UINTN kernel_elf_size_to_read = file_info->FileSize;
    void *kernel_elf_ptr = (void *)kernel_elf_buffer;
    status = kernel_file->Read(kernel_file, &kernel_elf_size_to_read, kernel_elf_ptr);
    if (kernel_elf_size_to_read != file_info->FileSize) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Did not load entire file!\r\n");
    }
    check_EFI_error(status, L"Unable to read kernel elf file!", SystemTable);
    SystemTable->conout->OutputString(SystemTable->conout, L"Opened and read kernel elf!\r\n");

    //Quick and dirty ELF parser/loader.
    Elf64_Ehdr *ELF_file_header = (Elf64_Ehdr *)kernel_elf_ptr;

    //The 0x0102464c457f is the little-endian version of the actual values that are supposed to be in the array from index 0 - 5.
    if ((*(uint64_t *)(ELF_file_header->e_ident) & 0xFFFFFFFFFFFF) != 0x0102464c457f) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Kernel ELF file is not valid!\r\n");
        while (1) __asm__ volatile ("hlt");
        return 0;
    }

    //Start pulling the program headers, we only care about the PT_LOAD for now. 
    Elf64_Phdr *ELF_program_header = (Elf64_Phdr *)((uint8_t *)kernel_elf_ptr + ELF_file_header->e_phoff);
    uint64_t physical_base = 0x2000000;
    uint64_t virtual_base;
    uint64_t kernel_entry_point = ELF_file_header->e_entry;
    uint8_t found_base = 0;
    for (int i = 0; i < ELF_file_header->e_phnum; i++) {
        //1 is the PT_LOAD type.
        if (ELF_program_header[i].p_type != 1) continue;
        if (!found_base) {
            virtual_base = ELF_program_header[i].p_vaddr;
            found_base = 1;
        }

        //Get the source address.
        void *source_address = (uint8_t *)ELF_file_header + ELF_program_header[i].p_offset;

        //Allocate pages
        EFI_PHYSICAL_ADDRESS physical_offset = physical_base + (ELF_program_header[i].p_vaddr - virtual_base);
        status = SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, (ELF_program_header[i].p_memsz + 0x1FFFFF) / 0x200000, &physical_offset);
        check_EFI_error(status, L"Unable to allocate memory (huge page) for kernel section!", SystemTable);

        //Copy the "real data"
        if (ELF_program_header[i].p_filesz > 0) {
            SystemTable->BootServices->CopyMem((void *)physical_offset, source_address, ELF_program_header[i].p_filesz);
        }

        //Zero the rest if memory size is larger thatn the data we had in the file
        if (ELF_program_header[i].p_memsz > ELF_program_header[i].p_filesz) {
            UINTN bss_size = ELF_program_header[i].p_memsz - ELF_program_header[i].p_filesz;
            SystemTable->BootServices->SetMem((void *)(physical_offset + ELF_program_header[i].p_filesz), bss_size, 0);
        }
    }
    SystemTable->conout->OutputString(SystemTable->conout, L"Finished parsing and loading elf!\r\n");

    BootInfo->kernel_size = (uint64_t)ELF_file_header->e_phnum;
    BootInfo->kernel_location_physical = (uint64_t)physical_base;
    BootInfo->kernel_location_virtual = (uint64_t)virtual_base;

    //Prep our page tables. We will direct map all of memory.
    //Start with our PML4 table.
    page_table *PML4;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PML4);
    check_EFI_error(status, L"Cannot allocate page for PML4 table!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PML4, 4096, 0);
    BootInfo->PML4 = (uint64_t)PML4;
    
    SystemTable->conout->OutputString(SystemTable->conout, L"PML4 Address: ");
    SystemTable->conout->OutputString(SystemTable->conout, to_string_hex((uint64_t)PML4));
    SystemTable->conout->OutputString(SystemTable->conout, L"\r\n");

    //We need to get the memory map for the maximum ram address.
    UINTN map_size;
    UINTN descriptor_size;
    EFI_MEMORY_DESCRIPTOR *map = get_memory_map(&map_size, &descriptor_size, NULL, SystemTable);

    //Get a page to keep track of our page table addresses, for the PMM later on.
    uint64_t *page_table_addresses;
    uint32_t page_table_addresses_next_index = 0;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&page_table_addresses);
    check_EFI_error(status, L"Cannot allocate page for page table addresses array!", SystemTable);

    //Now, lets map the memory!
    uint8_t *map_ptr = (uint8_t*)map;
    for (uint64_t i = 0; i < map_size; i += descriptor_size) {
        map = (EFI_MEMORY_DESCRIPTOR*)map_ptr;
        if (map->Type != EfiConventionalMemory &&
        map->Type != EfiLoaderCode &&
        map->Type != EfiLoaderData &&
        map->Type != EfiBootServicesCode &&
        map->Type != EfiBootServicesData &&
        map->Type != EfiACPIReclaimMemory &&
        map->Type != EfiACPIMemoryNVS
        ) continue;

        uint64_t low_bound_phys = map->PhysicalStart & ~0x1FFFFFULL;
        uint64_t high_bound_phys = ((map->PhysicalStart + 4096 * map->NumberOfPages - 1) + 0x1FFFFFULL) & ~0x1FFFFFULL;

        for (uint64_t current_address = low_bound_phys; current_address <= high_bound_phys; current_address += 0x200000) {
            uint64_t current_address_virtual = current_address + DIRECT_MAP_BASE;
            uint16_t PML4_index = (current_address_virtual >> 39) & 0x1FF; //Should start at index 273
            uint16_t PDPT_index = (current_address_virtual >> 30) & 0x1FF; //0
            uint16_t PD_index = (current_address_virtual >> 21) & 0x1FF; //0

            page_table *PDPT;
            page_table *PD;

            if (!(PML4->entries[PML4_index].bits.present)) {
                status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PDPT);
                check_EFI_error(status, L"Cannot allocate page for next PDPT!", SystemTable);
                SystemTable->BootServices->SetMem((void*)PDPT, 4096, 0);
                page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PDPT;
                PML4->entries[PML4_index].bits.present = 1;
                PML4->entries[PML4_index].bits.writeable = 1;
                PML4->entries[PML4_index].bits.execute_disable = 1;
                PML4->entries[PML4_index].bits.physical_address = (uint64_t)PDPT >> 12;
            }
            else {
                PDPT = (page_table*)(PML4->entries[PML4_index].bits.physical_address << 12);
            }
            
            if (!(PDPT->entries[PDPT_index].bits.present)) {
                status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PD);
                check_EFI_error(status, L"Cannot allocate page for next PD!", SystemTable);
                SystemTable->BootServices->SetMem((void*)PD, 4096, 0);
                page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PD;
                PDPT->entries[PDPT_index].bits.present = 1;
                PDPT->entries[PDPT_index].bits.writeable = 1;
                PDPT->entries[PDPT_index].bits.physical_address = (uint64_t)PD >> 12;
            }
            else {
                PD = (page_table*)(PDPT->entries[PDPT_index].bits.physical_address << 12);
            }

            PD->entries[PD_index].bits.present = 1;
            PD->entries[PD_index].bits.writeable = 1;
            PD->entries[PD_index].bits.huge_page = 1;
            PD->entries[PD_index].bits.global = 1;
            PD->entries[PD_index].bits.physical_address = (current_address >> 12) & ~0x1FFULL;
        }
    } 

    SystemTable->conout->OutputString(SystemTable->conout, L"Finished direct mapping of memory!\r\n");

    //Map the actual kernel as well!
    uint16_t PML4_index = (KERNEL_VIRTUAL_BASE >> 39) & 0x1FF; //Should start at index 511
    uint16_t PDPT_index = (KERNEL_VIRTUAL_BASE >> 30) & 0x1FF; //510 (Before last GB)
    uint16_t PD_index = (KERNEL_VIRTUAL_BASE >> 21) & 0x1FF; //0

    //Make the required page tables
    page_table *PDPT;
    page_table *PD;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PDPT);
    check_EFI_error(status, L"Cannot allocate page for kernel's PDPT!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PDPT, 4096, 0);
    page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PDPT;

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PD);
    check_EFI_error(status, L"Cannot allocate page for kernel's PD!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PD, 4096, 0);
    page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PD;

    //Create the mapping in the higher tables
    PML4->entries[PML4_index].bits.present = 1;
    PML4->entries[PML4_index].bits.writeable = 1;
    PML4->entries[PML4_index].bits.physical_address = (uint64_t)PDPT >> 12;

    PDPT->entries[PDPT_index].bits.present = 1;
    PDPT->entries[PDPT_index].bits.writeable = 1;
    PDPT->entries[PDPT_index].bits.physical_address = (uint64_t)PD >> 12;

    //Map the first section
    PD->entries[PD_index].bits.present = 1;
    PD->entries[PD_index].bits.huge_page = 1;
    PD->entries[PD_index].bits.global = 1;
    PD->entries[PD_index].bits.physical_address = (physical_base >> 12) & ~0x1FFULL;

    //Map seccond section
    PD_index++;
    PD->entries[PD_index].bits.present = 1;
    PD->entries[PD_index].bits.writeable = 1;
    PD->entries[PD_index].bits.huge_page = 1;
    PD->entries[PD_index].bits.global = 1;
    PD->entries[PD_index].bits.execute_disable = 1;
    PD->entries[PD_index].bits.physical_address = ((physical_base + 0x200000) >> 12) & ~0x1FFULL;

    //Map the stack page
    PML4_index = ((KERNEL_STACK_BASE - 1) >> 39) & 0x1FF; //Should be at index 511
    PDPT_index = ((KERNEL_STACK_BASE - 1) >> 30) & 0x1FF; //509
    PD_index = ((KERNEL_STACK_BASE - 1) >> 21) & 0x1FF; //511

    //Check to see if these were already mapped (Could have been from kernel depends on address)
    if (!PML4->entries[PML4_index].bits.present) {
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PDPT);
        check_EFI_error(status, L"Cannot allocate page for kernel stack's PDPT!", SystemTable);
        SystemTable->BootServices->SetMem((void*)PDPT, 4096, 0);
        page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PDPT;
        PML4->entries[PML4_index].bits.present = 1;
        PML4->entries[PML4_index].bits.writeable = 1;
        PML4->entries[PML4_index].bits.physical_address = (uint64_t)PDPT >> 12;
    }
    else PDPT = (page_table*)(PML4->entries[PML4_index].bits.physical_address << 12);

    if (!PDPT->entries[PDPT_index].bits.present) {
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PD);
        check_EFI_error(status, L"Cannot allocate page for kernel stack's PD!", SystemTable);
        SystemTable->BootServices->SetMem((void*)PD, 4096, 0);
        page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PD;
        PDPT->entries[PDPT_index].bits.present = 1;
        PDPT->entries[PDPT_index].bits.writeable = 1;
        PDPT->entries[PDPT_index].bits.physical_address = (uint64_t)PD >> 12;
    }
    else PD = (page_table*)(PDPT->entries[PDPT_index].bits.physical_address << 12);

    //Allocate a page for the stack (huge, 2MB alligned)
    EFI_PHYSICAL_ADDRESS kernel_stack_physical_base = BootInfo->kernel_location_physical + (BootInfo->kernel_size * 512 * 4096);
    status = SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, 512, &kernel_stack_physical_base);
    check_EFI_error(status, L"Cannot allocate page for kernel's stack!", SystemTable);
    SystemTable->BootServices->SetMem((void*)kernel_stack_physical_base, 512 * 4096, 0);
    BootInfo->kernel_stack_location_physical = (uint64_t)kernel_stack_physical_base;

    PD->entries[PD_index].bits.present = 1;
    PD->entries[PD_index].bits.writeable = 1;
    PD->entries[PD_index].bits.huge_page = 1;
    PD->entries[PD_index].bits.global = 1;
    PD->entries[PD_index].bits.execute_disable = 1;
    PD->entries[PD_index].bits.physical_address = (kernel_stack_physical_base >> 12) & ~0x1FFULL;

    //Map the nasm bridge, needs to be identity mapped
    //Get the address of our function.
    uint64_t bridge_address = (uint64_t)jump_to_kernel;
    uint64_t assumed_bridge_end_address = bridge_address + 512;

    PML4_index = (bridge_address >> 39) & 0x1FF;
    PDPT_index = (bridge_address >> 30) & 0x1FF;
    PD_index = (bridge_address >> 21) & 0x1FF;

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PDPT);
    check_EFI_error(status, L"Cannot allocate page for kernel's PDPT!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PDPT, 4096, 0);
    page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PDPT;

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PD);
    check_EFI_error(status, L"Cannot allocate page for kernel's PD!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PD, 4096, 0);
    page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PD;

    PML4->entries[PML4_index].bits.present = 1;
    PML4->entries[PML4_index].bits.writeable = 1;
    PML4->entries[PML4_index].bits.physical_address = (uint64_t)PDPT >> 12;

    PDPT->entries[PDPT_index].bits.present = 1;
    PDPT->entries[PDPT_index].bits.writeable = 1;
    PDPT->entries[PDPT_index].bits.physical_address = (uint64_t)PD >> 12;

    PD->entries[PD_index].bits.present = 1;
    PD->entries[PD_index].bits.writeable = 1;
    PD->entries[PD_index].bits.huge_page = 1;
    PD->entries[PD_index].bits.global = 1;
    PD->entries[PD_index].bits.physical_address = (bridge_address >> 12) & ~0x1FFULL;

    if ((bridge_address >> 21) != (assumed_bridge_end_address >> 21)) {
        PML4_index = (assumed_bridge_end_address >> 39) & 0x1FF;
        PDPT_index = (assumed_bridge_end_address >> 30) & 0x1FF;
        PD_index = (assumed_bridge_end_address >> 21) & 0x1FF;

        if (!PML4->entries[PML4_index].bits.present) {
            status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PDPT);
            check_EFI_error(status, L"Cannot allocate page for kernel stack's PDPT!", SystemTable);
            SystemTable->BootServices->SetMem((void*)PDPT, 4096, 0);
            page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PDPT;
            PML4->entries[PML4_index].bits.present = 1;
            PML4->entries[PML4_index].bits.writeable = 1;
            PML4->entries[PML4_index].bits.physical_address = (uint64_t)PDPT >> 12;
        }
        else PDPT = (page_table*)(PML4->entries[PML4_index].bits.physical_address << 12);

        if (!PDPT->entries[PDPT_index].bits.present) {
            status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PD);
            check_EFI_error(status, L"Cannot allocate page for kernel stack's PD!", SystemTable);
            SystemTable->BootServices->SetMem((void*)PD, 4096, 0);
            page_table_addresses[page_table_addresses_next_index++] = (uint64_t)PD;
            PDPT->entries[PDPT_index].bits.present = 1;
            PDPT->entries[PDPT_index].bits.writeable = 1;
            PDPT->entries[PDPT_index].bits.physical_address = (uint64_t)PD >> 12;
        }
        else PD = (page_table*)(PDPT->entries[PDPT_index].bits.physical_address << 12);

        PD->entries[PD_index].bits.present = 1;
        PD->entries[PD_index].bits.writeable = 1;
        PD->entries[PD_index].bits.huge_page = 1;
        PD->entries[PD_index].bits.global = 1;
        PD->entries[PD_index].bits.physical_address = (assumed_bridge_end_address >> 12) & ~0x1FFULL;
    }

    SystemTable->conout->OutputString(SystemTable->conout, L"Finished kernel mapping of memory!\r\n");

    BootInfo->page_table_addresses = page_table_addresses;
    BootInfo->page_table_addresses_count = page_table_addresses_next_index;

    //After this DO NOT use UEFI functions to avoid modifying memory map.
    //Prepare the memory map get.
    UINTN map_key;

    map = get_memory_map(&map_size, &descriptor_size, &map_key, SystemTable);
    BootInfo->mmap = (uint64_t)map;
    BootInfo->mmap_size = (uint64_t)map_size;

    //We finally leave the UEFI space!
    status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);

    //Make sure the call worked! If not, then we print, but its fine since it failed anyways.
    check_EFI_error(status, L"ExitBootServices failed!", SystemTable);

    //Finally, we jump to the kernel.
    jump_to_kernel((uint64_t)PML4, kernel_entry_point, KERNEL_STACK_BASE, (void*)BootInfo);

    while (1) __asm__ volatile ("hlt");

    return 0;   
}

uint16_t *EFIAPI to_string(uint64_t input){
    static uint16_t buffer[64];
    uint8_t i = 0;
    if (input == 0) {
        buffer[i++] = 0x0030;
        buffer[i] = 0x0000;
        return buffer;
    }

    //Null terminator, so 64-1
    while (i < 63 && input > 0){
        buffer[i++] = 0x0030 + (input % 10);
        input /= 10;
    }

    buffer[i] = 0x0000;
    
    //We reverse the string, since it is reversed for now.
    uint8_t left_ptr = 0;
    uint8_t right_ptr = i - 1;

    while (left_ptr <= right_ptr) {
        if (left_ptr == right_ptr) {
            break;
        }
        uint16_t temp = buffer[left_ptr];
        buffer[left_ptr++] = buffer[right_ptr];
        buffer[right_ptr--] = temp;
    }

    return buffer;
}

uint16_t *EFIAPI to_string_hex(uint64_t input){
    static uint16_t buffer[17];
    static uint16_t hex_chars[] = {
        0x0030, //0
        0x0031, //1
        0x0032, //2
        0x0033, //3
        0x0034, //4
        0x0035, //5
        0x0036, //6
        0x0037, //7
        0x0038, //8
        0x0039, //9
        0x0041, //A
        0x0042, //B
        0x0043, //C
        0x0044, //D
        0x0045, //E
        0x0046  //F
    };

    uint8_t i = 0;
    //Null terminator, so 17-1
    while (i < 16) {
        uint8_t next_hex = input & 0xF;
        buffer[i++] = hex_chars[next_hex];
        input = input >> 4;
    }

    buffer[i] = 0x0000;
    
    //We reverse the string, since it is reversed for now.
    uint8_t left_ptr = 0;
    uint8_t right_ptr = i - 1;

    while (left_ptr <= right_ptr) {
        if (left_ptr == right_ptr) {
            break;
        }
        uint16_t temp = buffer[left_ptr];
        buffer[left_ptr++] = buffer[right_ptr];
        buffer[right_ptr--] = temp;
    }

    return buffer;
}

void check_EFI_error(EFI_STATUS status, uint16_t *error_message, EFI_SYSTEM_TABLE *SystemTable) {
    if (status != 0) {
        SystemTable->conout->OutputString(SystemTable->conout, error_message);
        SystemTable->conout->OutputString(SystemTable->conout, L" Error Hex: ");
        SystemTable->conout->OutputString(SystemTable->conout, to_string_hex(status));
        SystemTable->conout->OutputString(SystemTable->conout, L"\r\n");
        while (1) __asm__ volatile ("hlt");
    }
}

EFI_MEMORY_DESCRIPTOR *EFIAPI get_memory_map(UINTN *map_size, UINTN *descriptor_size, UINTN *map_key, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    uint32_t descriptor_version;
    EFI_STATUS status;
    UINTN map_key_alt;
    //This is here in case you dont need the key AKA not exiting boot services.
    if (!map_key) map_key = &map_key_alt;

    //Get the required size
    SystemTable->BootServices->GetMemoryMap(map_size, NULL, map_key, descriptor_size, &descriptor_version);

    //Allocating the memory for the buffer will affect memory and change the map. Add some more buffer.
    *map_size += 2 * (*descriptor_size);
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, (*map_size + 4095) / 4096, (EFI_PHYSICAL_ADDRESS*)&map);
    check_EFI_error(status, L"Unable to allocate memory for memory map!", SystemTable);

    //Get the actual map.
    status = SystemTable->BootServices->GetMemoryMap(map_size, map, map_key, descriptor_size, &descriptor_version);
    check_EFI_error(status, L"Unable to execute getmap!", SystemTable);

    if (status != 0) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Buffer was Still too small!\r\n");
    }

    return map;
}