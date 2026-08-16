.RECIPEPREFIX := >

BUILD = build
OBJ = $(BUILD)/obj
ISO_ROOT = $(BUILD)/iso

CC = gcc
LD = ld

CFLAGS = \
	-m32 \
	-std=gnu11 \
	-ffreestanding \
	-O2 \
	-Wall \
	-Wextra \
	-fno-pie \
	-fno-pic \
	-fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-fno-builtin \
	-Isource/kernel \
	-Isource/config \
	-Isource/arch/x86 \
	-Isource/drivers/input \
	-Isource/input \
	-Isource/fs \
	-Isource/font \
	-Isource/terminal \
	-Isource/shell \
	-Isource/lib


OBJECTS = \
	$(OBJ)/boot.o \
	$(OBJ)/kernel.o \
	$(OBJ)/system_config.o \
	$(OBJ)/memory.o \
	$(OBJ)/string.o \
	$(OBJ)/path.o \
	$(OBJ)/unicode.o \
	$(OBJ)/rootfont.o \
	$(OBJ)/rootfont_data.o \
	$(OBJ)/terminal.o \
	$(OBJ)/keyboard.o \
	$(OBJ)/keymap_latam.o \
	$(OBJ)/filesystem.o \
	$(OBJ)/shell.o


all: $(BUILD)/RootOS.iso


$(BUILD):
>mkdir -p $(BUILD)

$(OBJ)/system_config.o: source/config/system_config.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/config/system_config.c \
	-o $(OBJ)/system_config.o


$(OBJ)/rootfont.o: source/font/rootfont.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/font/rootfont.c \
	-o $(OBJ)/rootfont.o

$(OBJ):
>mkdir -p $(OBJ)

$(OBJ)/memory.o: source/lib/memory.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/memory.c \
	-o $(OBJ)/memory.o


$(OBJ)/string.o: source/lib/string.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/string.c \
	-o $(OBJ)/string.o


$(OBJ)/path.o: source/lib/path.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/path.c \
	-o $(OBJ)/path.o


$(OBJ)/unicode.o: source/lib/unicode.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/unicode.c \
	-o $(OBJ)/unicode.o

$(OBJ)/boot.o: source/arch/x86/boot.S | $(OBJ)
>$(CC) \
	-m32 \
	-ffreestanding \
	-fno-pie \
	-fno-pic \
	-c source/arch/x86/boot.S \
	-o $(OBJ)/boot.o


$(OBJ)/kernel.o: source/kernel/kernel.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/kernel/kernel.c \
	-o $(OBJ)/kernel.o


$(OBJ)/terminal.o: source/terminal/terminal.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/terminal/terminal.c \
	-o $(OBJ)/terminal.o


$(OBJ)/keyboard.o: source/drivers/input/keyboard.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/drivers/input/keyboard.c \
	-o $(OBJ)/keyboard.o


$(OBJ)/keymap_latam.o: source/input/keymap_latam.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/input/keymap_latam.c \
	-o $(OBJ)/keymap_latam.o


$(OBJ)/filesystem.o: source/fs/filesystem.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/fs/filesystem.c \
	-o $(OBJ)/filesystem.o


$(OBJ)/shell.o: source/shell/shell.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/shell/shell.c \
	-o $(OBJ)/shell.o

$(OBJ)/rootfont_data.o: $(BUILD)/rootfont.bin | $(OBJ)
>cd $(BUILD) && \
	objcopy \
		-I binary \
		-O elf32-i386 \
		-B i386 \
		rootfont.bin \
		obj/rootfont_data.o

$(BUILD)/kernel.elf: $(OBJECTS) linker.ld | $(BUILD)
>$(LD) \
	-m elf_i386 \
	-T linker.ld \
	-o $(BUILD)/kernel.elf \
	$(OBJECTS)


$(BUILD)/RootOS.iso: $(BUILD)/kernel.elf
>rm -rf $(ISO_ROOT)
>mkdir -p $(ISO_ROOT)/boot/grub
>cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
>cp grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
>grub-mkrescue \
	-o $(BUILD)/RootOS.iso \
	$(ISO_ROOT)

$(BUILD)/rootfont.bin: rootfs/system/fonts/unifont_all-17.0.05.hex | $(BUILD)
>python3 tools/build_unifont.py \
	rootfs/system/fonts/unifont_all-17.0.05.hex \
	$(BUILD)/rootfont.bin

check: $(BUILD)/kernel.elf
>grub-file \
	--is-x86-multiboot \
	$(BUILD)/kernel.elf
>@echo "RootOS: kernel Multiboot valido"


run: $(BUILD)/RootOS.iso
>qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-m 128M


clean:
>rm -rf $(BUILD)


clear: clean


rebuild: clean all


.PHONY: all run check clean clear rebuild
