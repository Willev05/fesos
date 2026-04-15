#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
{0x9042a9de,0x23dc,0x4a38,\
{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}}

//Basic types
typedef void* EFI_HANDLE;
typedef uint64_t EFI_STATUS;
#define NULL ((void *)0)
typedef unsigned long long UINTN; //Like uint64_t
typedef uint64_t EFI_PHYSICAL_ADDRESS; //An address in memory
typedef uint64_t EFI_VIRTUAL_ADDRESS;

//GUID type
typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

//Setup for text protocol
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

//Function to write text to screen, needs reference to _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, and string is UTF-16 string to write. Note: UEFI uses \r\n for newline.
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    uint16_t *String
);

//The simple text output protocol. Contains the OutputString function.
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* _unused;
    EFI_TEXT_STRING OutputString; // The function we need
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

//The locate protocol function. Protocol is the GUID, registration is idk, interface is a reference to a pointer for the protocol.
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL) (
    EFI_GUID *Protocol,
    void *Registration,
    void **Interface
);

//The stall method. Like sleep, but microseconds instead.
typedef EFI_STATUS (EFIAPI *EFI_STALL) (
    UINTN Microseconds
);

//Possible pixel format, we hope it is PixelBlueGreenRedReserved8BitPerColor;
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

//Information for mode described next in file.
typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    uint32_t _unused[4];
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

//Actual data for GOP.
typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

//The GOP. We will need to send this data to the Kernel.
typedef struct {
    void *_unused[3];
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

//The memory map type.
typedef struct {
    uint32_t Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

//Gets the memory map.
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP) (
    UINTN *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR *MemoryMap,
    UINTN *MapKey,
    UINTN *DescriptorSize,
    uint32_t *DescriptorVersion
);

//The boot services struct.
typedef struct {
    //Table header
    char _unused[24];

    //Task Priority Services
    void *_unused2[2];

    //Memory Services
    void *_unused3[2];
    EFI_GET_MEMORY_MAP GetMemoryMap;
    void *_unused12[2];

    //Event & Timer Services
    void *_unused4[6];

    //Protocol Handler Services
    void *_unused5[9];

    //Image services
    void *_unused6[5];

    //Miscellaneous Services
    void *_unused7;
    EFI_STALL Stall;
    void *_unused11;

    //DriverSupport Services
    void *_unused8[2];

    //Open and Close Protocol Services
    void *_unused9[3];

    //Library Services
    void *_unused10[2];
    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

//The main struct passed to us from UEFI loader.
typedef struct {
    char _unused[52];
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout; // ConOut pointer
    void* _unused3; // StandardErrorHandle
    void* _unused4; // StdErr
    void* _unused5; // RuntimeServices
    EFI_BOOT_SERVICES* BootServices;
} EFI_SYSTEM_TABLE;

//Our entry point.
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);