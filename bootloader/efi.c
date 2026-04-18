#include "efi.h"
#include "../shared/elf.h"

uint16_t *EFIAPI to_string(uint64_t input);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        (uint16_t *)L"Whats up!\r\n"
    );

    EFI_STATUS status = 0;

    //Get the linear pixel buffer address.
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface = NULL;
    SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, (void**)&gop_interface);

    //Get a file handle on the drive root.
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system;
    EFI_FILE_PROTOCOL *root;

    EFI_GUID loaded_image_protocol_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    SystemTable->BootServices->HandleProtocol(ImageHandle, &loaded_image_protocol_guid, (void**)&loaded_image);

    //We do this to get the file system protocol for the specific drive this image was loaded from.
    EFI_GUID file_system_protocol_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    SystemTable->BootServices->HandleProtocol(loaded_image->DeviceHandle, &file_system_protocol_guid, (void**)&file_system);

    //We get the root of the drive from the file system.
    file_system->OpenVolume(file_system, &root);

    //Now, we open the kernel core file, which sits on the root.
    EFI_FILE_PROTOCOL *kernel_file;
    root->Open(root, &kernel_file, L"kernel_core.elf", 1, 0);

    //Get the file size
    UINTN info_size = 0;
    EFI_FILE_INFO *file_info;
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

    //We need to get the size of the struct, since the text is variable length.
    kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, NULL);

    //With the size, we request the memory, and load the file info.
    SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (void **)&file_info);
    kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, file_info);

    //We allocate the space needed to load the entire file.
    UINTN kernel_elf_required_pages = (file_info->FileSize + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS kernel_elf_buffer;
    SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, kernel_elf_required_pages, &kernel_elf_buffer);

    //Load the entire thing.
    UINTN kernel_elf_size_to_read = file_info->FileSize;
    void *kernel_elf_ptr = (void *)kernel_elf_buffer;
    kernel_file->Read(kernel_file, &kernel_elf_size_to_read, kernel_elf_ptr);

    //Quick and dirty ELF parser/loader.
    Elf64_Ehdr *ELF_file_header = (Elf64_Ehdr *)kernel_elf_ptr;

    //The 0x0102464c457f is the little-endian version of the actual values that are supposed to be in the array from index 0 - 5.
    if ((*(uint64_t *)(ELF_file_header->e_ident) & 0xFFFFFFFFFFFF) != 0x0102464c457f) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Kernel ELF file is not valid!\r\n");
        while (1);
        return 0;
    }

    //Start pulling the program headers, we only care about the PT_LOAD for now. 
    Elf64_Phdr *ELF_program_header = (Elf64_Phdr *)((uint8_t *)kernel_elf_ptr + ELF_file_header->e_phoff);
    uint64_t physical_base = 0x1000000;
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
        SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, (ELF_program_header[i].p_memsz + 4095) / 4096, &physical_offset);

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

    //Prepare the memory map get.
    UINTN MapSize = 0;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    uint32_t DescriptorVersion;

    //Get the required size
    status = SystemTable->BootServices->GetMemoryMap(&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);

    //Allocating the memory for the buffer will affect memory and change the map. Add some more buffer.
    MapSize += 2 * DescriptorSize;
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, (void**)&Map);

    //Get the actual map.
    status = SystemTable->BootServices->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);

    if (status != 0) {
        SystemTable->conout->OutputString(SystemTable->conout, L"Buffer was Still too small :(\r\n");
    }
    SystemTable->conout->OutputString(SystemTable->conout, to_string(DescriptorSize));

    while (1);

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