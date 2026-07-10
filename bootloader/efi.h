/* Copyright (C) 2026 William Lévesque */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
{0x9042a9de,0x23dc,0x4a38,\
{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}}

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
{0x5B1B31A1,0x9562,0x11d2,\
{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}}

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
{0x0964e5b22,0x6459,0x11d2,\
{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_FILE_INFO_ID \
{0x09576e92,0x6d3f,0x11d2,\
{0x8e, 0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

//Basic types
typedef void* EFI_HANDLE;
typedef uint64_t EFI_STATUS;
typedef unsigned long long UINTN; //Like uint64_t
typedef uint64_t EFI_PHYSICAL_ADDRESS; //An address in memory
typedef uint64_t EFI_VIRTUAL_ADDRESS;

//Memory Types
typedef enum {
   EfiReservedMemoryType,
   EfiLoaderCode,
   EfiLoaderData,
   EfiBootServicesCode,
   EfiBootServicesData,
   EfiRuntimeServicesCode,
   EfiRuntimeServicesData,
   EfiConventionalMemory,
   EfiUnusableMemory,
   EfiACPIReclaimMemory,
   EfiACPIMemoryNVS,
   EfiMemoryMappedIO,
   EfiMemoryMappedIOPortSpace,
   EfiPalCode,
   EfiPersistentMemory,
   EfiUnacceptedMemoryType,
   EfiMaxMemoryType
} EFI_MEMORY_TYPE;

//Allocation type for allocate pages
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

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
    EFI_MEMORY_TYPE Type;
    uint32_t pad;
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

//Allocate memory dynamically.
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL) (
    EFI_MEMORY_TYPE PoolType,
    UINTN Size,
    void **Buffer
);

//Locate the protocol for a specific handle.
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL) (
    EFI_HANDLE Handle,
    EFI_GUID *Protocol,
    void **Interface
);

struct _EFI_SYSTEM_TABLE;

//Loaded image protocol, usually got from the function above with passing the ImageHandle.
typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    struct _EFI_SYSTEM_TABLE *SystemTable;

    //Source of the image (We need ts for drive)
    EFI_HANDLE DeviceHandle;
    void *FilePath; //Should be EFI_DEVICE_PATH_PROTOCOL if i ever implement.
    void *Reserved;

    //Image's load options
    uint32_t LoadOptionsSize;
    void *LoadOptions;

    //Location where image was loaded (in memory)
    void *ImageBase;
    uint64_t ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload; //Should be EFI_IMAGE_UNLOAD if i ever implement.
} EFI_LOADED_IMAGE_PROTOCOL;

//File protocol related functions and such. Absolute pain.
struct _EFI_FILE_PROTOCOL;

//Opens a requested file. This works by passing a new handle to the requested file.
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN) (
    struct _EFI_FILE_PROTOCOL *This,
    struct _EFI_FILE_PROTOCOL **NewHandle,
    uint16_t *FileName,
    uint64_t OpenMode,
    uint64_t Attribute
);

//Buffer size will be used as in/out. Pass in the size of your buffer, and afeter the function, it will have the amount of data written in buffer.
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ) (
    struct _EFI_FILE_PROTOCOL *This,
    UINTN *BufferSize,
    void *Buffer
);

//Buffer size will be used as in/out. Pass in the size of your buffer, and afeter the function, it will have the amount of data written in buffer.
//Returns different info (file, volume) based on passed GUID.
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO) (
    struct _EFI_FILE_PROTOCOL *This,
    EFI_GUID *InformationType,
    UINTN *BufferSize,
    void *Buffer
);

typedef struct _EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_FILE_OPEN Open;
    void *_unused[2];
    EFI_FILE_READ Read;
    void *_unused2[3];
    EFI_FILE_GET_INFO GetInfo;
    void *_unused3[6];
} EFI_FILE_PROTOCOL;

typedef struct {
    uint8_t data[16];
} EFI_TIME_PLACEHOLDER;

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME_PLACEHOLDER CreateTime;
    EFI_TIME_PLACEHOLDER LastAccessedTime;
    EFI_TIME_PLACEHOLDER ModificationTime;
    uint64_t Attribute;
    uint16_t FileName[];
} EFI_FILE_INFO;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME) (
    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    EFI_FILE_PROTOCOL **Root
);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

//Allocates pages instead of a simple pool.
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES) (
    EFI_ALLOCATE_TYPE Type,
    EFI_MEMORY_TYPE MemoryType,
    UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory
);

//Copies memory.
typedef void (EFIAPI *EFI_COPY_MEM) (
    void *Destination,
    void *Source,
    UINTN Length
);

//Sets memory.
typedef void (EFIAPI *EFI_SET_MEM) (
    void *Buffer,
    UINTN Size,
    uint8_t Value
);

//Exits the boot services. This gives control over to the kernel.
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES) (
    EFI_HANDLE ImageHandle,
    UINTN MapKey
);

//The boot services struct.
typedef struct {
    //Table header
    char _unused[24];

    //Task Priority Services
    void *_unused2[2];

    //Memory Services
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    void *_unused12;

    //Event & Timer Services
    void *_unused4[6];

    //Protocol Handler Services
    void *_unused5[3];
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *_unused13[5];

    //Image services
    void *_unused6[4];
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

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
    void *_unused14[2];

    //32-bit CRC Services
    void *_unused15;

    //Miscelaneous Services
    EFI_COPY_MEM CopyMem;
    EFI_SET_MEM SetMem;
} EFI_BOOT_SERVICES;

//The main struct passed to us from UEFI loader.
typedef struct _EFI_SYSTEM_TABLE{
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