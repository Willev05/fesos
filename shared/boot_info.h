#include <stdint.h>

//The memory types
typedef enum {
   ReservedMemoryType,
   LoaderCode,
   LoaderData,
   BootServicesCode,
   BootServicesData,
   RuntimeServicesCode,
   RuntimeServicesData,
   ConventionalMemory,
   UnusableMemory,
   ACPIReclaimMemory,
   ACPIMemoryNVS,
   MemoryMappedIO,
   MemoryMappedIOPortSpace,
   PalCode,
   PersistentMemory,
   UnacceptedMemoryType,
   MaxMemoryType
} MEMORY_TYPE;

//The memory map type.
typedef struct {
    MEMORY_TYPE Type;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} MEMORY_DESCRIPTOR_bi;

//The kernel size is in 2MB pages!
typedef struct {
    void *framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixels_per_scan_line;
    void *mmap;
    uint64_t mmap_size;
    uint64_t descriptor_size;
    void *kernel_location_physical;
    void *kernel_location_virtual;
    uint64_t kernel_size;
    void *page_table_virtal;
} boot_info;