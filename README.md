# fesos (Hobby Kernel)

A custom, from-scratch 64-bit operating system kernel designed for educational exploration of low-level systems architecture.

The goal of this project is to build a microkernel from first principles.

## Current Architecture & Features

- **Bootloader:** UEFI-compliant environment utilizing the official UEFI 2.10 specification mappings.
- **Architecture:** x86_64 target execution environment.
- **Memory Management:** 
  - Custom heap bucket allocator (`kmalloc`/`kfree`) targeting 16-byte up to 1024-byte allocation zones.
  - Virtual Memory Allocator supporting multiple memory trees (AVL) for future multiprocessing support and quick search for demand paging.
  - Virtual Memory Manager / Paging layer implementing demand paging through a page fault handler.
  - Physical Memory Manager utilizing a bitmap to track in-use pages.

## Project Roadmap

- [x] UEFI environment setup and boot table parsing
- [x] Memory allocation systems (physical and virtual) with heap allocator (kmalloc)
- [ ] Virtual File System (VFS) layout
- [ ] Basic FAT32 Driver with AHCI Driver
- [ ] Multitasking & Context Switching (Ring 3 userspace isolation)
- [ ] Porting standard C library headers via custom sysroot stubs
- [ ] Migration of drivers to userspace for proper microkernel architecture

## Building and Running

### Prerequisites

To compile and emulate this kernel, you will need a cross-compiler toolchain and QEMU:
- `mtools` (for creating bootable disk images)
- `nasm` (for assembly files)
- `clang` & `lld` cross-compiler and linker
- `qemu-system-x86_64` & `ovmf` emulator and efi image

### Compilation & Emulation

```bash
# Clone the repository
git clone https://github.com/Willev05/fesos.git
cd fesos

# Compile the kernel
make

# Make the image
make image

# Launch inside QEMU with debug file enabled
make run
