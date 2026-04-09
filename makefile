CC = clang
LD = lld-link

COMMON_FLAGS = -ffreestanding -fno-stack-protector -mno-red-zone -Wall

# Bootloader specific
BOOT_CFLAGS = $(COMMON_FLAGS) -fshort-wchar -O2 -target x86_64-unknown-windows-coff
BOOT_LDFLAGS = /subsystem:efi_application \
				/entry:efi_main \
				/nodefaultlib

BUILD_DIR = build
BOOTLOADER_OUT_DIR = $(BUILD_DIR)/iso/EFI/BOOT

$(BOOTLOADER_OUT_DIR)/BOOTX64.EFI: $(BUILD_DIR)/bootloader.o
	@mkdir -p $(BOOTLOADER_OUT_DIR)
	$(LD) $(BOOT_LDFLAGS) /out:$@ $<

$(BUILD_DIR)/bootloader.o: bootloader/efi.h bootloader/efi.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BOOT_CFLAGS) -c bootloader/efi.c -o $@

clean:
	rm -f $(BUILD_DIR)/*.*

run:
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
                   -net none \
                   -drive format=raw,file=fat:rw:build/iso