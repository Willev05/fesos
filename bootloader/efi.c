#include "efi.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    SystemTable->conout->OutputString(
        SystemTable->conout,
        (uint16_t *)L"Whats up!"
    );


    while (1);

    return 0;   
}