/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "efi.h"
#include "../kernel/x86_64/include/kernel/elf.h"
#include "../kernel/x86_64/include/kernel/boot_info.h"
#include "../kernel/x86_64/include/memory/vmm.h"
#include "../kernel/x86_64/include/common/stdtypes.h"

uint16_t *EFIAPI to_string(uint64_t input);
uint16_t *EFIAPI to_string_hex(uint64_t input);
void EFIAPI check_EFI_error(EFI_STATUS status, uint16_t *error_message, EFI_SYSTEM_TABLE *SystemTable);
EFI_MEMORY_DESCRIPTOR *EFIAPI get_memory_map(UINTN *map_size, UINTN *descriptor_size, UINTN *map_key, EFI_SYSTEM_TABLE *SystemTable);
static void EFIAPI set_bitmap_bit(uint64_t *bitmap, uint64_t bit_number);
static void EFIAPI unset_bitmap_bit(uint64_t *bitmap, uint64_t bit_number);
static uint8_t EFIAPI get_bitmap_bit(uint64_t *bitmap, uint64_t bit_number);
page_table EFIAPI *walk_and_crate_next_table(uint64_t *bitmap, EFI_SYSTEM_TABLE *sys_table, page_table *table, uint16_t index);
void EFIAPI map_vaddr_to_paddr(uint64_t *bitmap, EFI_SYSTEM_TABLE *sys_table, page_table *PML4, uint64_t vaddr, uint64_t paddr, uint64_t flags);

extern void __attribute__((sysv_abi)) jump_to_kernel(uint64_t pml4_phys, uint64_t entry_point, uint64_t stack_top, void *BootInfo);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        L"EFI image loaded!\r\n"
    );

    EFI_STATUS status = 0;

    //We need to start by getting the UEFI memory map. Used for getting the max valid address for conventional memory.
    UINTN map_size;
    UINTN descriptor_size;
    EFI_MEMORY_DESCRIPTOR *map = get_memory_map(&map_size, &descriptor_size, NULL, SystemTable);

    //We then get the max memory address of proper EFI_MEMORY_TYPE.
    uint64_t max_memory_address = 0;
    uint64_t physical_memory_frame_count;
    uint64_t physical_memory_used_frame_count;
    uint8_t *map_ptr = (uint8_t*)map;
    for (uint64_t i = 0; i < map_size; i += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *map = (EFI_MEMORY_DESCRIPTOR*)(map_ptr + i);
        if (map->Type != EfiConventionalMemory &&
        map->Type != EfiLoaderCode &&
        map->Type != EfiLoaderData &&
        map->Type != EfiBootServicesCode &&
        map->Type != EfiBootServicesData &&
        map->Type != EfiACPIReclaimMemory &&
        map->Type != EfiACPIMemoryNVS &&
        map->Type != EfiRuntimeServicesCode &&
        map->Type != EfiRuntimeServicesData
        ) continue;

        //If we are here, then it is part of the total system ram.
        physical_memory_frame_count += map->NumberOfPages;

        if (map->Type != EfiConventionalMemory &&
        map->Type != EfiLoaderCode &&
        map->Type != EfiLoaderData &&
        map->Type != EfiBootServicesCode &&
        map->Type != EfiBootServicesData
        ) {
            //This is the used memory.
            physical_memory_used_frame_count += map->NumberOfPages;
            continue;
        }
        //Here is the actual legal memory for us to use.
        max_memory_address = (map->PhysicalStart + 4096 * map->NumberOfPages > max_memory_address) ? map->PhysicalStart + 4096 * map->NumberOfPages : max_memory_address;
    }

    //Now, with the max address, we can simply request UEFI for the pages to store it. We will ask for sequential memory for easy reading like an array.
    //Calculate the size of our bitmap and allocate it for later use.
    uint64_t bitmap_size_bytes = (((max_memory_address + 4095) / 4096) + 7) / 8;
    uint64_t bitmap_frame_count = ((max_memory_address + 4095) / 4096) >> 12;
    EFI_PHYSICAL_ADDRESS bitmap_paddr;
    volatile uint64_t *bitmap;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, (bitmap_size_bytes + 4095) / 4096, &bitmap_paddr);
    check_EFI_error(status, L"Cannot allocate pages for memory bitmap!", SystemTable);
    //Initialized to 1 -> mark as busy unless told otherwise. Handles potential holes and MMIO, etc.
    bitmap = (uint64_t*)bitmap_paddr;
    SystemTable->BootServices->SetMem((void*)bitmap, bitmap_size_bytes, 255);
    
    //Now, we will loop the map, which we will request again from uefi. We will unset bits which can be declared as free. We will unset UEFI loader stuff, since the bitmap will be used ONLY for the kernel, which wont care about it.
    map = get_memory_map(&map_size, &descriptor_size, NULL, SystemTable);
    map_ptr = (uint8_t*)map;
    for (uint64_t i = 0; i < map_size; i += descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)(map_ptr + i);
        if (mmap->Type != EfiConventionalMemory &&
        mmap->Type != EfiLoaderCode &&
        mmap->Type != EfiLoaderData &&
        mmap->Type != EfiBootServicesCode &&
        mmap->Type != EfiBootServicesData
        ) continue;

        uint64_t bit_to_change = ((mmap->PhysicalStart + 4095) & ~4095ULL) >> 12;
        uint64_t higher_bit = mmap->PhysicalStart + (mmap->NumberOfPages * 0x1000) >> 12;

        while (bit_to_change < higher_bit) unset_bitmap_bit(bitmap, bit_to_change++);
    }

    //Allocate a page for boot struct which we will be filling up.
    boot_info *BootInfo;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS *)&BootInfo);
    check_EFI_error(status, L"Could not allocate a page for the boot struct!", SystemTable);

    //Start filling the BI struct with some data.
    BootInfo->memory_bitmap_address = (uint64_t)(bitmap_paddr);
    BootInfo->memory_bitmap_size = bitmap_size_bytes;
    BootInfo->memory_bitmap_frame_count = bitmap_frame_count;
    BootInfo->memory_physical_total_frames = physical_memory_frame_count;
    BootInfo->memory_physical_used_frames = physical_memory_used_frame_count;

    //Now, we can note down any kernel permenent things for the PMM to track. This includes the very bitmap! So we will note it down.
    uint64_t bit_to_change = (uint64_t)(bitmap_paddr) >> 12;
    for (uint64_t i = 0; i < (bitmap_size_bytes + 4095) / 4096; i++) {
        set_bitmap_bit(bitmap, bit_to_change++);
        BootInfo->memory_physical_used_frames++;
    } 

    //And also set the first frame to 0 as a sentinel value, and handle like "NULL"/invalid returns from PMM.
    if (!get_bitmap_bit(bitmap, 0)) {
        set_bitmap_bit(bitmap, 0);
        BootInfo->memory_physical_used_frames++;
    }
    
    //The kernel cares about the struct, so save it to the bitmap.
    set_bitmap_bit(bitmap, (uint64_t)(BootInfo) >> 12);
    BootInfo->memory_physical_used_frames++;

    //Prep our PML4 table.
    page_table *PML4;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&PML4);
    check_EFI_error(status, L"Cannot allocate page for PML4 table!", SystemTable);
    SystemTable->BootServices->SetMem((void*)PML4, 4096, 0);
    BootInfo->PML4 = (uint64_t)PML4;

    //We also save this table to bitmap.
    set_bitmap_bit(bitmap, (uint64_t)(PML4) >> 12);
    BootInfo->memory_physical_used_frames++;

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
    status = root->Open(root, &kernel_file, L"kcore.elf", 1, 0);
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

    //We will loop through all sections in the ELF file.
    for (uint16_t phindex = 0; phindex < ELF_file_header->e_phnum; phindex++) {
        if (ELF_program_header[phindex].p_type != 1) { // 1 == PT_LOAD
            continue;
        }
        uint64_t current_file_segment_consumption = 0;
        //This loop will handle creating each frame required for the section, and mapping it to virtual.
        for (uint64_t segment_vaddr = ELF_program_header[phindex].p_vaddr; segment_vaddr < ELF_program_header[phindex].p_vaddr + ELF_program_header[phindex].p_memsz; segment_vaddr += 0x1000) {
            //We then need to get a frame.
            uint8_t *kernel_section_frame_ptr;
            status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &kernel_section_frame_ptr);
            check_EFI_error(status, L"Unable to allocate page for kernel!", SystemTable);

            //This will copy AT MOST one page, or less if needed.
            uint64_t size_to_copy = (ELF_program_header[phindex].p_filesz - current_file_segment_consumption >= 4096) ? 4096 : ELF_program_header[phindex].p_filesz - current_file_segment_consumption;

            //We then need to copy the data, at the size requested.
            SystemTable->BootServices->CopyMem((void*)kernel_section_frame_ptr, (void*)((uint64_t)ELF_file_header + ELF_program_header[phindex].p_offset + current_file_segment_consumption), size_to_copy);
            //If not an entire frame was copied, then fill the rest with 0s.
            if (size_to_copy < 4096) {
                SystemTable->BootServices->SetMem((void*)(kernel_section_frame_ptr + size_to_copy), 4096 - size_to_copy, 0);
            }
            //Then update this so we copy the next frame/data.
            current_file_segment_consumption += size_to_copy;

            //Now, we map the frame.
            //We get the flags from the data.
            uint8_t write_flag = (ELF_program_header[phindex].p_flags >> 1) & 1;
            uint8_t nx_flag = !(ELF_program_header[phindex].p_flags & 1) << 6;
            map_vaddr_to_paddr(bitmap, SystemTable, PML4, segment_vaddr, (uint64_t)kernel_section_frame_ptr, PT_GLOBAL | write_flag | nx_flag);

            //The kernel needs to remember where it is in physical mem, so save it to the bitmap.
            set_bitmap_bit(bitmap, (uint64_t)(kernel_section_frame_ptr) >> 12);
            BootInfo->memory_physical_used_frames++;
        }
    }
    SystemTable->conout->OutputString(SystemTable->conout, L"Finished parsing and loading elf!\r\n");

    //We need to get the memory map again for the up-to-date version.
    map = get_memory_map(&map_size, &descriptor_size, NULL, SystemTable);

    //Now, lets map the memory (deirect map)!
    map_ptr = (uint8_t*)map;
    for (uint64_t i = 0; i < map_size; i += descriptor_size) {
        map = (EFI_MEMORY_DESCRIPTOR*)(map_ptr + i);
        if (map->Type != EfiConventionalMemory &&
        map->Type != EfiLoaderCode &&
        map->Type != EfiLoaderData &&
        map->Type != EfiBootServicesCode &&
        map->Type != EfiBootServicesData &&
        map->Type != EfiACPIReclaimMemory
        ) continue;

        uint64_t low_bound_phys = (map->PhysicalStart + 4095) & ~0xFFFULL;
        uint64_t high_bound_phys = map->PhysicalStart + (map->NumberOfPages * 0x1000) & ~0xFFFULL;

        for (uint64_t current_address = low_bound_phys; current_address < high_bound_phys; current_address += 0x1000) {
            uint64_t current_address_virtual = current_address + DIRECT_MAP_BASE;

            map_vaddr_to_paddr(bitmap, SystemTable, PML4, current_address_virtual, current_address, PT_GLOBAL | PT_WRITEABLE);
        }
    }
    SystemTable->conout->OutputString(SystemTable->conout, L"Finished direct mapping of memory!\r\n");

    //Allocate n pages for the kernel and then map them to the designated kernel stack area
    uint64_t next_kernel_stack_vaddr = KERNEL_STACK_BASE;
    for (int i = 0; i < KERNEL_STACK_PAGE_COUNT; i++) {
        EFI_PHYSICAL_ADDRESS kernel_stack_paddr;
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &kernel_stack_paddr);
        check_EFI_error(status, L"Cannot allocate page for kernel's stack!", SystemTable);
        //The -1 here is due to how the stack works in x86_64. It grows down and never touches its base, so -1 will make the address fall into the page under which wil actually be used.
        map_vaddr_to_paddr(bitmap, SystemTable, PML4, next_kernel_stack_vaddr - 1, kernel_stack_paddr, PT_GLOBAL | PT_WRITEABLE);
    }

    //TODO: Work on the bridge identity mapping here.
    //They represent the beggining and end of the GDT. Remember to use & to get the address, as the labels themselves hold no useful data for us.
    extern uint8_t gdt64;
    extern uint8_t gdt64_ptr; 

    //From these, we get the lower/higher bounds of pages to identity map.
    uint64_t lower_address_bound = (uint64_t)(&gdt64) & ~0xFFFULL;
    uint64_t higher_address_bound = ((uint64_t)(&gdt64_ptr) + 0x1000) & ~0xFFFULL;

    //Then, we map the pages. This loop should map 1-2 pages depending on if it crosses a page boundary.
    for (uint64_t current_address = lower_address_bound; current_address < higher_address_bound; lower_address_bound += 0x1000) {
        map_vaddr_to_paddr(bitmap, SystemTable, PML4, current_address, current_address, PT_GLOBAL);
    }

    SystemTable->conout->OutputString(SystemTable->conout, L"Finished kernel mapping of memory!\r\n");

    //After this DO NOT use UEFI functions to avoid modifying memory map.
    //Prepare the memory map get.
    UINTN map_key;
    map = get_memory_map(&map_size, &descriptor_size, &map_key, SystemTable);

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

void EFIAPI check_EFI_error(EFI_STATUS status, uint16_t *error_message, EFI_SYSTEM_TABLE *SystemTable) {
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

static void EFIAPI set_bitmap_bit(uint64_t *bitmap,uint64_t bit_number) {
    bitmap[bit_number / 8] = bitmap[bit_number / 8] | (1 << (bit_number % 8));
}

static void EFIAPI unset_bitmap_bit(uint64_t *bitmap, uint64_t bit_number) {
    bitmap[bit_number / 8] = bitmap[bit_number / 8] & ((254 << (bit_number % 8)) | ((1 << (bit_number % 8)) - 1));
}

static uint8_t EFIAPI get_bitmap_bit(uint64_t *bitmap, uint64_t bit_number) {
    return (bitmap[bit_number / 8] >> (bit_number % 8) & 0x1);
}

page_table EFIAPI *walk_and_crate_next_table(uint64_t *bitmap, EFI_SYSTEM_TABLE *sys_table, page_table *table, uint16_t index) {
    page_table *next_table = NULL;
    EFI_STATUS status;
    if (!table->entries[index].bits.present) {
        //Allocate a page table
        EFI_PHYSICAL_ADDRESS next_table_p;
        status = sys_table->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &next_table_p);
        check_EFI_error(status, L"Unable to allocate page for a page table!", sys_table);
        next_table = (page_table*)(next_table_p + DIRECT_MAP_BASE);
        sys_table->BootServices->SetMem((void*)next_table, 4096, 0);

        //And prep it.
        table->entries[index].bits.present = 1;
        table->entries[index].bits.writeable = 1;
        table->entries[index].bits.user_available = 1;
        table->entries[index].bits.physical_address = next_table_p >> 12;
    }
    else next_table = (page_table*)((table->entries[index].bits.physical_address << 12) + DIRECT_MAP_BASE);

    return next_table;
}

void EFIAPI map_vaddr_to_paddr(uint64_t *bitmap, EFI_SYSTEM_TABLE *sys_table, page_table *PML4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint16_t PML4_index = (vaddr >> 39) & 0x1FF;
    uint16_t PDPT_index = (vaddr >> 30) & 0x1FF;
    uint16_t PD_index = (vaddr >> 21) & 0x1FF;

    page_table *PDPT = walk_and_crate_next_table(bitmap, sys_table, PML4, PML4_index);
    page_table *PD = walk_and_crate_next_table(bitmap, sys_table, PDPT, PDPT_index);
    
    //Here, we need to check if this will be a huge page or not.
    if (flags & 0x10) {
        //We assume the VMA or other caller will have the addresses alligned to the 2MB mark.
        PD->entries[PD_index].bits.present = 1;
        PD->entries[PD_index].bits.writeable = (flags & 0x1);
        PD->entries[PD_index].bits.user_available = (flags & 0x2) >> 1;
        PD->entries[PD_index].bits.write_through = (flags & 0x4) >> 2;
        PD->entries[PD_index].bits.disable_caching = (flags & 0x8) >> 3;
        PD->entries[PD_index].bits.huge_page = 1;
        PD->entries[PD_index].bits.global = (flags & 0x20) >> 5;
        PD->entries[PD_index].bits.execute_disable = (flags & 0x40) >> 6;

        PD->entries[PD_index].bits.physical_address = (paddr >> 12) & ~0x1FFULL;
    }
        
    page_table *PT = vmm_walk_and_crate_next_table(PD, PD_index);

    uint64_t PT_index = (vaddr >> 12) & 0x1FF;

    PT->entries[PT_index].bits.present = 1;
    PT->entries[PT_index].bits.writeable = (flags & 0x1);
    PT->entries[PT_index].bits.user_available = (flags & 0x2) >> 1;
    PT->entries[PT_index].bits.write_through = (flags & 0x4) >> 2;
    PT->entries[PT_index].bits.disable_caching = (flags & 0x8) >> 3;
    PT->entries[PT_index].bits.global = (flags & 0x20) >> 5;
    PT->entries[PT_index].bits.execute_disable = (flags & 0x40) >> 6;

    PT->entries[PT_index].bits.physical_address = (paddr >> 12);
}