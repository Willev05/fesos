CC      = x86_64-linux-gnu-gcc
LD      = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

COMMON_FLAGS = -ffreestanding -fno-stack-protector -mno-red-zone -Wall

# Bootloader specific
BOOT_CFLAGS = $(COMMON_FLAGS) -fshort-wchar -fpic
BOOT_LDFLAGS = -shared -Bsymbolic -T bootloader/bootloader_linker.ld

MTOOLS_IMG = uefi.img

image: BOOTX64.EFI
	rm -f $(MTOOLS_IMG)
	dd if=/dev/zero of=$(MTOOLS_IMG) bs=1M count=64
	mformat -i $(MTOOLS_IMG) ::
	mmd -i $(MTOOLS_IMG) ::/EFI
	mmd -i $(MTOOLS_IMG) ::/EFI/BOOT
	mcopy -i $(MTOOLS_IMG) BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	sync

BOOTX64.EFI: bootloader.so
	# Link directly to the final EFI file
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	
	# Set the correct Subsystem and Target flags to ensure UEFI compliance
	$(OBJCOPY) --target=efi-app-x86_64 --subsystem=10 $@ $@
	
	# Force the size to be a multiple of 4KB to prevent "Not an Image"
	truncate -s %4096 $@

bootloader.so: bootloader.o
	$(LD) $(BOOT_LDFLAGS) $< -o $@

bootloader.o: bootloader/efi.h bootloader/efi.c
	$(CC) $(BOOT_CFLAGS) -c bootloader/efi.c -o $@

clean:
	rm -f *.o *.so *.EFI