#include "efi.h"
#include "../shared/elf.h"
#include "../shared/boot_info.h"

uint16_t *EFIAPI to_string(uint64_t input);
uint16_t *EFIAPI to_string_hex(uint64_t input);
void check_EFI_error(EFI_STATUS status, uint16_t *error_message, EFI_SYSTEM_TABLE *SystemTable);
void InspectMemoryNear16MB(EFI_SYSTEM_TABLE *SystemTable);

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
    BootInfo->framebuffer_base = (void *)gop_interface->Mode->FrameBufferBase;
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
        status = SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, (ELF_program_header[i].p_memsz + 4095) / 4096, &physical_offset);
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
    BootInfo->kernel_location_physical = (void*)physical_base;
    BootInfo->kernel_location_virtual = (void*)virtual_base;

    //After this DO NOT use UEFI functions to avoid modifying memory map.
    //Prepare the memory map get.
    UINTN MapSize = 0;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    uint32_t DescriptorVersion;

    //Get the required size
    SystemTable->BootServices->GetMemoryMap(&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);

    //Allocating the memory for the buffer will affect memory and change the map. Add some more buffer.
    MapSize += 2 * DescriptorSize;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, (MapSize + 4095) / 4096, (EFI_PHYSICAL_ADDRESS*)&Map);
    check_EFI_error(status, L"Unable to allocate memory for memory map!", SystemTable);

    //Get the actual map.
    status = SystemTable->BootServices->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
    check_EFI_error(status, L"Unable to execute getmap!", SystemTable);

    if (status != 0) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Buffer was Still too small!\r\n");
    }

    BootInfo->mmap = (void*)Map;
    BootInfo->mmap_size = (uint64_t)MapSize;

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