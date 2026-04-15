#include "efi.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        (uint16_t *)L"Whats up!\r\n"
    );

    //Get the linear pixel buffer address.
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface = NULL;
    SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, (void**)&gop_interface);


    while (1);

    return 0;   
}