.RECIPEPREFIX := >


# ============================================================
# ROOTOS BUILD CONFIGURATION
# ============================================================

BUILD = build
OBJ = $(BUILD)/obj
ISO_ROOT = $(BUILD)/iso


# ============================================================
# BUILD TOOLS
# ============================================================

CC = gcc
LD = ld
OBJCOPY = objcopy
PYTHON = python3

LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)


# ============================================================
# COMPILER FLAGS
# ============================================================

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
	-Isource/drivers/timer \
	-Isource/input \
	-Isource/fs \
	-Isource/font \
	-Isource/display \
	-Isource/terminal \
	-Isource/editor \
	-Isource/shell \
	-Isource/lib


# ============================================================
# KERNEL OBJECTS
# ============================================================

OBJECTS = \
	$(OBJ)/boot.o \
	$(OBJ)/interrupt_stubs.o \
	$(OBJ)/kernel.o \
	$(OBJ)/interrupts.o \
	$(OBJ)/pic.o \
	$(OBJ)/pit.o \
	$(OBJ)/time.o \
	$(OBJ)/system_config.o \
	$(OBJ)/memory.o \
	$(OBJ)/string.o \
	$(OBJ)/path.o \
	$(OBJ)/unicode.o \
	$(OBJ)/rootfont.o \
	$(OBJ)/rootfont_data.o \
	$(OBJ)/rootdisplay.o \
	$(OBJ)/terminal.o \
	$(OBJ)/ps2.o \
	$(OBJ)/keyboard.o \
	$(OBJ)/mouse.o \
	$(OBJ)/keymap_latam.o \
	$(OBJ)/rootinput.o \
	$(OBJ)/filesystem.o \
	$(OBJ)/rootedit.o \
	$(OBJ)/shell.o


# ============================================================
# DEFAULT TARGET
# ============================================================

all: $(BUILD)/RootOS.iso


# ============================================================
# BUILD DIRECTORIES
# ============================================================

$(BUILD):
>mkdir -p $(BUILD)


$(OBJ):
>mkdir -p $(OBJ)


# ============================================================
# BOOT
# ============================================================

$(OBJ)/boot.o: source/arch/x86/boot.S | $(OBJ)
>$(CC) \
	-m32 \
	-ffreestanding \
	-fno-pie \
	-fno-pic \
	-c source/arch/x86/boot.S \
	-o $(OBJ)/boot.o


# ============================================================
# INTERRUPT STUBS
# ============================================================

$(OBJ)/interrupt_stubs.o: source/arch/x86/interrupt_stubs.S | $(OBJ)
>$(CC) \
	-m32 \
	-ffreestanding \
	-fno-pie \
	-fno-pic \
	-c source/arch/x86/interrupt_stubs.S \
	-o $(OBJ)/interrupt_stubs.o


# ============================================================
# KERNEL
# ============================================================

$(OBJ)/kernel.o: source/kernel/kernel.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/kernel/kernel.c \
	-o $(OBJ)/kernel.o


# ============================================================
# INTERRUPTS
# ============================================================

$(OBJ)/interrupts.o: source/arch/x86/interrupts.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/arch/x86/interrupts.c \
	-o $(OBJ)/interrupts.o


# ============================================================
# PIC
# ============================================================

$(OBJ)/pic.o: source/arch/x86/pic.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/arch/x86/pic.c \
	-o $(OBJ)/pic.o


# ============================================================
# PIT
# ============================================================

$(OBJ)/pit.o: source/drivers/timer/pit.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/drivers/timer/pit.c \
	-o $(OBJ)/pit.o


# ============================================================
# ROOT TIME
# ============================================================

$(OBJ)/time.o: source/kernel/time.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/kernel/time.c \
	-o $(OBJ)/time.o


# ============================================================
# SYSTEM CONFIG
# ============================================================

$(OBJ)/system_config.o: source/config/system_config.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/config/system_config.c \
	-o $(OBJ)/system_config.o


# ============================================================
# MEMORY
# ============================================================

$(OBJ)/memory.o: source/lib/memory.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/memory.c \
	-o $(OBJ)/memory.o


# ============================================================
# STRING
# ============================================================

$(OBJ)/string.o: source/lib/string.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/string.c \
	-o $(OBJ)/string.o


# ============================================================
# PATH
# ============================================================

$(OBJ)/path.o: source/lib/path.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/path.c \
	-o $(OBJ)/path.o


# ============================================================
# UNICODE
# ============================================================

$(OBJ)/unicode.o: source/lib/unicode.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/lib/unicode.c \
	-o $(OBJ)/unicode.o


# ============================================================
# ROOTFONT GENERATOR
# ============================================================

$(BUILD)/rootfont.bin: assets/fonts/unifont_all-17.0.05.hex | $(BUILD)
>$(PYTHON) tools/build_unifont.py \
	assets/fonts/unifont_all-17.0.05.hex \
	$(BUILD)/rootfont.bin


# ============================================================
# ROOTFONT
# ============================================================

$(OBJ)/rootfont.o: source/font/rootfont.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/font/rootfont.c \
	-o $(OBJ)/rootfont.o


# ============================================================
# ROOTFONT DATA
# ============================================================

$(OBJ)/rootfont_data.o: $(BUILD)/rootfont.bin | $(OBJ)
>cd $(BUILD) && \
	$(OBJCOPY) \
		-I binary \
		-O elf32-i386 \
		-B i386 \
		rootfont.bin \
		obj/rootfont_data.o


# ============================================================
# DISPLAY
# ============================================================

$(OBJ)/rootdisplay.o: source/display/rootdisplay.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/display/rootdisplay.c \
	-o $(OBJ)/rootdisplay.o


# ============================================================
# TERMINAL
# ============================================================

$(OBJ)/terminal.o: source/terminal/terminal.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/terminal/terminal.c \
	-o $(OBJ)/terminal.o


# ============================================================
# PS/2
# ============================================================

$(OBJ)/ps2.o: source/drivers/input/ps2.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/drivers/input/ps2.c \
	-o $(OBJ)/ps2.o


# ============================================================
# KEYBOARD
# ============================================================

$(OBJ)/keyboard.o: source/drivers/input/keyboard.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/drivers/input/keyboard.c \
	-o $(OBJ)/keyboard.o


# ============================================================
# MOUSE
# ============================================================

$(OBJ)/mouse.o: source/drivers/input/mouse.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/drivers/input/mouse.c \
	-o $(OBJ)/mouse.o


# ============================================================
# LATAM KEYMAP
# ============================================================

$(OBJ)/keymap_latam.o: source/input/keymap_latam.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/input/keymap_latam.c \
	-o $(OBJ)/keymap_latam.o


# ============================================================
# ROOTINPUT
# ============================================================

$(OBJ)/rootinput.o: source/input/rootinput.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/input/rootinput.c \
	-o $(OBJ)/rootinput.o


# ============================================================
# FILESYSTEM
# ============================================================

$(OBJ)/filesystem.o: source/fs/filesystem.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/fs/filesystem.c \
	-o $(OBJ)/filesystem.o


# ============================================================
# ROOTEDIT
# ============================================================

$(OBJ)/rootedit.o: source/editor/rootedit.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/editor/rootedit.c \
	-o $(OBJ)/rootedit.o


# ============================================================
# SHELL
# ============================================================

$(OBJ)/shell.o: source/shell/shell.c | $(OBJ)
>$(CC) $(CFLAGS) \
	-c source/shell/shell.c \
	-o $(OBJ)/shell.o


# ============================================================
# LINK KERNEL
# ============================================================

$(BUILD)/kernel.elf: $(OBJECTS) linker.ld | $(BUILD)
>$(LD) \
	-m elf_i386 \
	-T linker.ld \
	-o $(BUILD)/kernel.elf \
	$(OBJECTS) \
	$(LIBGCC)


# ============================================================
# CREATE ISO
# ============================================================

$(BUILD)/RootOS.iso: $(BUILD)/kernel.elf
>rm -rf $(ISO_ROOT)

>mkdir -p $(ISO_ROOT)

>cp -a rootfs/. $(ISO_ROOT)/

>mkdir -p $(ISO_ROOT)/boot/grub

>cp $(BUILD)/kernel.elf \
	$(ISO_ROOT)/boot/kernel.elf

>cp grub/grub.cfg \
	$(ISO_ROOT)/boot/grub/grub.cfg

>grub-mkrescue \
	-o $(BUILD)/RootOS.iso \
	$(ISO_ROOT)


# ============================================================
# CHECK MULTIBOOT
# ============================================================

check: $(BUILD)/kernel.elf
>grub-file \
	--is-x86-multiboot \
	$(BUILD)/kernel.elf

>@echo "RootOS: kernel Multiboot valido"


# ============================================================
# RUN
# ============================================================

run: $(BUILD)/RootOS.iso
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-m 128M \
	-vga std


# ============================================================
# CLEAN
# ============================================================

clean:
>rm -rf $(BUILD)


# ============================================================
# CLEAR
# ============================================================

clear: clean


# ============================================================
# REBUILD
# ============================================================

rebuild: clean all


# ============================================================
# PHONY
# ============================================================

.PHONY: \
	all \
	run \
	check \
	clean \
	clear \
	rebuild