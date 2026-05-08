CC = clang
BOOT_LD = lld-link
KERNEL_LD = ld.lld
AS = nasm

COMMON_FLAGS = -ffreestanding -fno-stack-protector -mno-red-zone -Wall -Wextra

# Bootloader specific
BOOT_CFLAGS = $(COMMON_FLAGS) -fshort-wchar -O2 -target x86_64-unknown-windows-coff
BOOT_LDFLAGS = /subsystem:efi_application \
				/entry:efi_main \
				/nodefaultlib
BOOT_ASFLAGS = -f win64

KERNEL_CFLAGS = $(COMMON_FLAGS) -O2 -target x86_64-unknown-none-elf -mcmodel=kernel
KERNEL_LDFLAGS = -T kernel/x86_64/kernel_core_linker_script.ld

BUILD_DIR = build
BOOTLOADER_OUT_DIR = $(BUILD_DIR)/iso/EFI/BOOT
KERNEL_CORE_OUT_DIR = $(BUILD_DIR)/iso

.PHONY: all clean run
all: $(BOOTLOADER_OUT_DIR)/BOOTX64.EFI  $(KERNEL_CORE_OUT_DIR)/kernel_core.elf

$(KERNEL_CORE_OUT_DIR)/kernel_core.elf: $(BUILD_DIR)/kernel_core.o $(BUILD_DIR)/serial.o
	@mkdir -p $(KERNEL_CORE_OUT_DIR)
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $^ -o $@

$(BUILD_DIR)/kernel_core.o: kernel/x86_64/kernel_core.c kernel/x86_64/drivers/serial.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: kernel/x86_64/drivers/serial.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BOOTLOADER_OUT_DIR)/BOOTX64.EFI: $(BUILD_DIR)/bootloader.o $(BUILD_DIR)/jump_to_kernel.o
	@mkdir -p $(BOOTLOADER_OUT_DIR)
	$(BOOT_LD) $(BOOT_LDFLAGS) /out:$@ $^

$(BUILD_DIR)/bootloader.o: bootloader/efi.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BUILD_DIR)/jump_to_kernel.o: bootloader/jump_to_kernel.nasm
	@mkdir -p $(BUILD_DIR)
	$(AS) $(BOOT_ASFLAGS) $< -o $@

clean:
	rm -f $(BUILD_DIR)/*.*

run:
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
                   -net none \
                   -drive format=raw,file=fat:rw:build/iso \
				   -d int,cpu_reset -D qemu.log \
				   -serial stdio