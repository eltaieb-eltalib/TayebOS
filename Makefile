# TayebOS - build a bootable floppy image
CC      = gcc
CFLAGS  = -m32 -ffreestanding -fno-pic -fno-stack-protector -nostdlib -Wall -Wextra
LD      = ld
NASM    = nasm

all: tayebos.img tayebos-usb.img

boot/boot.bin: boot/boot.asm
	$(NASM) -f bin $< -o $@

kernel/kernel_entry.o: kernel/kernel_entry.asm
	$(NASM) -f elf32 $< -o $@

kernel/isr.o: kernel/isr.asm
	$(NASM) -f elf32 $< -o $@

kernel/kernel.o: kernel/kernel.c kernel/interrupts.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/interrupts.o: kernel/interrupts.c kernel/interrupts.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/tasks.o: kernel/tasks.asm
	$(NASM) -f elf32 $< -o $@

kernel/kernel.bin: kernel/kernel_entry.o kernel/isr.o kernel/kernel.o kernel/interrupts.o kernel/tasks.o kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -o kernel/kernel.elf kernel/kernel_entry.o kernel/isr.o kernel/kernel.o kernel/interrupts.o kernel/tasks.o
	objcopy -O binary kernel/kernel.elf $@

tayebos.img: boot/boot.bin kernel/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=boot/boot.bin of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=kernel/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	@echo "Built $@"

# 16 MB raw image for USB sticks: write it with Rufus in DD mode
# (or dd / balenaEtcher). Boots on Legacy/CSM BIOS from USB-HDD emulation.
tayebos-usb.img: boot/boot.bin kernel/kernel.bin
	dd if=/dev/zero of=$@ bs=1M count=16 status=none
	dd if=boot/boot.bin of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=kernel/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	@echo "Built $@ - flash it with Rufus (DD mode)"

run-usb: tayebos-usb.img
	qemu-system-i386 -drive format=raw,file=tayebos-usb.img,if=ide -boot c

run: tayebos.img
	qemu-system-i386 -drive format=raw,file=tayebos.img,if=floppy

run-serial: tayebos.img
	qemu-system-i386 -drive format=raw,file=tayebos.img,if=floppy -nographic -serial mon:stdio

clean:
	rm -f boot/*.bin kernel/*.o kernel/*.elf kernel/*.bin tayebos.img tayebos-usb.img

.PHONY: all run run-serial clean
