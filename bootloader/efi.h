#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef void* EFI_HANDLE;
typedef uint64_t EFI_STATUS;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    uint16_t *String
);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* _unused;
    EFI_TEXT_STRING OutputString; // The function we need
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    char _unused[64];
    // We'll add the File Protocol and Memory Map pointers here later
} EFI_BOOT_SERVICES;

typedef struct {
    char _unused[52];
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout; // ConOut pointer
    void* _unused3; // StandardErrorHandle
    void* _unused4; // StdErr
    void* _unused5; // RuntimeServices
    EFI_BOOT_SERVICES* BootServices;
} EFI_SYSTEM_TABLE;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);