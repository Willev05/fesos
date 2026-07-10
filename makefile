# Copyright (C) 2026 William Lévesque
# SPDX-License-Identifier: GPL-3.0-or-later

#Directories
BUILD_DIR := build
BOOTLOADER_OUT_DIR := $(BUILD_DIR)/iso/EFI/BOOT
KERNEL_CORE_OUT_DIR := $(BUILD_DIR)/iso

KERNEL_CORE_SRC_DIR := kernel/x86_64
BOOTLOADER_SRC_DIR := bootloader

KERNEL_CORE_OBJ_DIR := $(BUILD_DIR)/kernel_core_objects
BOOTLOADER_OBJ_DIR := $(BUILD_DIR)/bootloader_objects

#Targets
KERNEL_CORE := $(KERNEL_CORE_OUT_DIR)/kcore.elf
BOOTLOADER := $(BOOTLOADER_OUT_DIR)/BOOTX64.EFI

#Toolchain
CC := clang
BOOT_LD := lld-link
KERNEL_LD := ld.lld
AS := nasm

#Flags
COMMON_CFLAGS := -g -ffreestanding -fno-stack-protector -mno-red-zone -Wall -Wextra

BOOT_CFLAGS := $(COMMON_CFLAGS) -fshort-wchar -O2 -target x86_64-unknown-windows-coff
BOOT_LDFLAGS := /subsystem:efi_application \
				/entry:efi_main \
				/nodefaultlib
BOOT_ASFLAGS := -f win64

KERNEL_CFLAGS := $(COMMON_CFLAGS) -O2 -target x86_64-unknown-none-elf -mcmodel=kernel
KERNEL_LDFLAGS := -T kernel/x86_64/kernel_core_linker_script.ld
KERNEL_ASFLAGS := -f elf64

#Kernel core related discoveries
KERNEL_CORE_SOURCES := $(shell find $(KERNEL_CORE_SRC_DIR) -name '*.c' -o -name '*.nasm')
KERNEL_CORE_OBJECTS := $(addprefix $(KERNEL_CORE_OBJ_DIR)/, $(patsubst %.nasm, %.o, $(patsubst %.c, %.o, $(KERNEL_CORE_SOURCES))))

.PHONY: all clean run run-dbg image

all: $(KERNEL_CORE) $(BOOTLOADER)

#Link kernel core
$(KERNEL_CORE): $(KERNEL_CORE_OBJECTS)
	@echo "LD  $@"
	@mkdir -p $(dir $@)
	@$(KERNEL_LD) $(KERNEL_LDFLAGS) $^ -o $@

#Compile kernel core files
$(KERNEL_CORE_OBJ_DIR)/%.o: %.c
	@echo "CC  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@

#Don't forget the assembly files too
$(KERNEL_CORE_OBJ_DIR)/%.o: %.nasm
	@echo "AS $<"
	@mkdir -p $(dir $@)
	@$(AS) $(KERNEL_ASFLAGS) $< -o $@

#Link bootloader
$(BOOTLOADER): $(BOOTLOADER_OBJ_DIR)/bootloader.o $(BOOTLOADER_OBJ_DIR)/jump_to_kernel.o
	@echo "LD  $@"
	@mkdir -p $(dir $@)
	@$(BOOT_LD) $(BOOT_LDFLAGS) /out:$@ $^

#Compile bootloader
$(BOOTLOADER_OBJ_DIR)/bootloader.o: $(BOOTLOADER_SRC_DIR)/efi.c
	@echo "CC  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BOOTLOADER_OBJ_DIR)/jump_to_kernel.o: $(BOOTLOADER_SRC_DIR)/jump_to_kernel.nasm
	@echo "AS $<"
	@mkdir -p $(dir $@)
	@$(AS) $(BOOT_ASFLAGS) $< -o $@

#Run QEMU and create image file
image:
	rm -f build/disk.img
	@RAW_SIZE=$$(($$(du -s -m build/iso | cut -f1) + 12)); \
	SIZE_MB=$$((RAW_SIZE < 48 ? 48 : RAW_SIZE)); \
	echo "Building disk.img with dynamic size: $$SIZE_MB MB"; \
	dd if=/dev/zero of=build/disk.img bs=1M count=$$SIZE_MB
	parted -s --align optimal build/disk.img mklabel gpt
	parted -s --align optimal build/disk.img mkpart ESP fat32 1MiB 100%
	parted -s build/disk.img set 1 esp on
	mformat -i build/disk.img@@1M -F
	mcopy -i build/disk.img@@1M -s build/iso/* ::/

run:
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
                	-M q35 -net none -m 4G \
					-device ahci,id=ahci \
                	-drive id=disk,format=raw,file=build/disk.img,if=none \
					-device ide-hd,drive=disk,bus=ahci.0 \
					-d int,cpu_reset,guest_errors -D qemu.log \
					-serial stdio

run-dbg: 
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -s -S \
                	-M q35 -net none -m 4G \
					-device ahci,id=ahci \
                	-drive id=disk,format=raw,file=build/disk.img,if=none \
					-device ide-hd,drive=disk,bus=ahci.0 \
					-d int,cpu_reset,guest_errors -D qemu.log \
					-serial stdio -no-reboot -no-shutdown \


clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)

debug:
	@echo "Sources: $(KERNEL_CORE_SOURCES)"
	@echo "Objects: $(KERNEL_CORE_OBJECTS)"