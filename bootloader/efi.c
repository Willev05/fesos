#include "efi.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        (uint16_t *)L"Whats up!\r\n"
    );

    SystemTable->BootServices->Stall(5000000);

    //Get the linear pixel buffer address.
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *gop_interface = NULL;
    EFI_STATUS status = SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, &gop_interface);

    if (status == 0) {
        SystemTable->conout->OutputString(SystemTable->conout, (uint16_t *)L"Found GOP!\r\n");
    } else {
        SystemTable->conout->OutputString(SystemTable->conout, (uint16_t *)L"GOP NOT found.\r\n");
    }

    while (1);

    return 0;   
}