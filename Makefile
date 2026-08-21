.RECIPEPREFIX := >


# ============================================================
# ROOTOS BUILD CONFIGURATION
# ============================================================

BUILD = build
OBJ = $(BUILD)/obj
ISO_ROOT = $(BUILD)/iso
ROOT_DISK = disk/rootos.img
DEMO_ELF = $(BUILD)/apps/hello.elf
DEMO_PACKAGE = $(BUILD)/packages/Hello-1.0.rtpgk
USB_TEST_DISK = disk/usb-test.img
USB_BUS ?=
USB_ADDR ?=
DEVICE ?=
DRIVER_BUILD = $(BUILD)/drivers
DRIVERPACK = $(BUILD)/driverpack.rdp
E1000_RTDRV = $(DRIVER_BUILD)/e1000.rtdrv
RTL8169_RTDRV = $(DRIVER_BUILD)/rtl8169.rtdrv


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
	-Isource/drivers/storage \
	-Isource/drivers/pci \
	-Isource/drivers/usb \
	-Isource/drivers/net \
	-Isource/usb \
	-Isource/input \
	-Isource/storage \
	-Isource/fs \
	-Isource/font \
	-Isource/display \
	-Isource/terminal \
	-Isource/editor \
	-Isource/shell \
	-Isource/process \
	-Isource/app \
	-Isource/package \
	-Isource/device \
	-Isource/driver \
	-Isource/network \
	-Isdk/include \
	-Isource/lib

DRIVER_CFLAGS = \
	-m32 \
	-std=gnu11 \
	-ffreestanding \
	-O2 \
	-Wall \
	-Wextra \
	-Werror \
	-fno-pie \
	-fno-pic \
	-fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-fno-builtin \
	-Isdk/include

APP_CFLAGS = \
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
	-fno-unwind-tables \
	-fno-builtin \
	-Isdk/include


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
	$(OBJ)/heap.o \
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
	$(OBJ)/ata_pio.o \
	$(OBJ)/pci.o \
	$(OBJ)/device_manager.o \
	$(OBJ)/net_device.o \
	$(OBJ)/elf_module.o \
	$(OBJ)/driver_api.o \
	$(OBJ)/driver_store.o \
	$(OBJ)/usb.o \
	$(OBJ)/xhci.o \
	$(OBJ)/usb_mass_storage.o \
	$(OBJ)/rndis.o \
	$(OBJ)/block_device.o \
	$(OBJ)/net.o \
	$(OBJ)/arp.o \
	$(OBJ)/ipv4.o \
	$(OBJ)/udp.o \
	$(OBJ)/dhcp.o \
	$(OBJ)/dns.o \
	$(OBJ)/tcp.o \
	$(OBJ)/rootstorage.o \
	$(OBJ)/rootfs_disk.o \
	$(OBJ)/keymap_latam.o \
	$(OBJ)/rootinput.o \
	$(OBJ)/rootclipboard.o \
	$(OBJ)/roottext.o \
	$(OBJ)/filesystem.o \
	$(OBJ)/process.o \
	$(OBJ)/rootapi.o \
	$(OBJ)/elf_loader.o \
	$(OBJ)/app_manager.o \
	$(OBJ)/rtpgk.o \
	$(OBJ)/package_manager.o \
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
# BOOT / INTERRUPTS
# ============================================================

$(OBJ)/boot.o: source/arch/x86/boot.S | $(OBJ)
>$(CC) -m32 -ffreestanding -fno-pie -fno-pic \
	-c source/arch/x86/boot.S \
	-o $(OBJ)/boot.o

$(OBJ)/interrupt_stubs.o: source/arch/x86/interrupt_stubs.S | $(OBJ)
>$(CC) -m32 -ffreestanding -fno-pie -fno-pic \
	-c source/arch/x86/interrupt_stubs.S \
	-o $(OBJ)/interrupt_stubs.o

$(OBJ)/interrupts.o: source/arch/x86/interrupts.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/arch/x86/interrupts.c -o $(OBJ)/interrupts.o

$(OBJ)/pic.o: source/arch/x86/pic.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/arch/x86/pic.c -o $(OBJ)/pic.o


# ============================================================
# KERNEL / TIMER / CONFIG
# ============================================================

$(OBJ)/kernel.o: source/kernel/kernel.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/kernel/kernel.c -o $(OBJ)/kernel.o

$(OBJ)/pit.o: source/drivers/timer/pit.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/timer/pit.c -o $(OBJ)/pit.o

$(OBJ)/time.o: source/kernel/time.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/kernel/time.c -o $(OBJ)/time.o

$(OBJ)/heap.o: source/kernel/heap.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/kernel/heap.c -o $(OBJ)/heap.o

$(OBJ)/system_config.o: source/config/system_config.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/config/system_config.c -o $(OBJ)/system_config.o


# ============================================================
# ROOT LIBRARY
# ============================================================

$(OBJ)/memory.o: source/lib/memory.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/lib/memory.c -o $(OBJ)/memory.o

$(OBJ)/string.o: source/lib/string.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/lib/string.c -o $(OBJ)/string.o

$(OBJ)/path.o: source/lib/path.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/lib/path.c -o $(OBJ)/path.o

$(OBJ)/unicode.o: source/lib/unicode.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/lib/unicode.c -o $(OBJ)/unicode.o


# ============================================================
# FONT / DISPLAY / TERMINAL
# ============================================================

$(BUILD)/rootfont.bin: assets/fonts/unifont_all-17.0.05.hex | $(BUILD)
>$(PYTHON) tools/build_unifont.py \
	assets/fonts/unifont_all-17.0.05.hex \
	$(BUILD)/rootfont.bin

$(OBJ)/rootfont.o: source/font/rootfont.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/font/rootfont.c -o $(OBJ)/rootfont.o

$(OBJ)/rootfont_data.o: $(BUILD)/rootfont.bin | $(OBJ)
>cd $(BUILD) && \
	$(OBJCOPY) \
		-I binary \
		-O elf32-i386 \
		-B i386 \
		rootfont.bin \
		obj/rootfont_data.o

$(OBJ)/rootdisplay.o: source/display/rootdisplay.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/display/rootdisplay.c -o $(OBJ)/rootdisplay.o

$(OBJ)/terminal.o: source/terminal/terminal.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/terminal/terminal.c -o $(OBJ)/terminal.o


# ============================================================
# INPUT DRIVERS / ROOT INPUT
# ============================================================

$(OBJ)/ps2.o: source/drivers/input/ps2.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/input/ps2.c -o $(OBJ)/ps2.o

$(OBJ)/keyboard.o: source/drivers/input/keyboard.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/input/keyboard.c -o $(OBJ)/keyboard.o

$(OBJ)/mouse.o: source/drivers/input/mouse.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/input/mouse.c -o $(OBJ)/mouse.o

$(OBJ)/keymap_latam.o: source/input/keymap_latam.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/input/keymap_latam.c -o $(OBJ)/keymap_latam.o

$(OBJ)/rootinput.o: source/input/rootinput.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/input/rootinput.c -o $(OBJ)/rootinput.o

$(OBJ)/rootclipboard.o: source/input/rootclipboard.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/input/rootclipboard.c -o $(OBJ)/rootclipboard.o

$(OBJ)/roottext.o: source/input/roottext.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/input/roottext.c -o $(OBJ)/roottext.o


# ============================================================
# STORAGE / FILESYSTEM
# ============================================================

$(OBJ)/ata_pio.o: source/drivers/storage/ata_pio.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/storage/ata_pio.c -o $(OBJ)/ata_pio.o

$(OBJ)/pci.o: source/drivers/pci/pci.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/pci/pci.c -o $(OBJ)/pci.o

$(OBJ)/device_manager.o: source/device/device_manager.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/device/device_manager.c -o $(OBJ)/device_manager.o

$(OBJ)/net_device.o: source/network/net_device.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/net_device.c -o $(OBJ)/net_device.o

$(OBJ)/elf_module.o: source/driver/elf_module.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/driver/elf_module.c -o $(OBJ)/elf_module.o

$(OBJ)/driver_api.o: source/driver/driver_api.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/driver/driver_api.c -o $(OBJ)/driver_api.o

$(OBJ)/driver_store.o: source/driver/driver_store.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/driver/driver_store.c -o $(OBJ)/driver_store.o

$(OBJ)/usb.o: source/usb/usb.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/usb/usb.c -o $(OBJ)/usb.o

$(OBJ)/xhci.o: source/drivers/usb/xhci.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/usb/xhci.c -o $(OBJ)/xhci.o

$(OBJ)/usb_mass_storage.o: source/drivers/usb/usb_mass_storage.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/usb/usb_mass_storage.c -o $(OBJ)/usb_mass_storage.o

$(OBJ)/rndis.o: source/drivers/usb/rndis.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/drivers/usb/rndis.c -o $(OBJ)/rndis.o

$(OBJ)/block_device.o: source/storage/block_device.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/storage/block_device.c -o $(OBJ)/block_device.o

# ============================================================
# NETWORK
# ============================================================

$(OBJ)/net.o: source/network/net.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/net.c -o $(OBJ)/net.o

$(OBJ)/arp.o: source/network/arp.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/arp.c -o $(OBJ)/arp.o

$(OBJ)/ipv4.o: source/network/ipv4.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/ipv4.c -o $(OBJ)/ipv4.o

$(OBJ)/udp.o: source/network/udp.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/udp.c -o $(OBJ)/udp.o

$(OBJ)/dhcp.o: source/network/dhcp.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/dhcp.c -o $(OBJ)/dhcp.o

$(OBJ)/dns.o: source/network/dns.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/dns.c -o $(OBJ)/dns.o

$(OBJ)/tcp.o: source/network/tcp.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/network/tcp.c -o $(OBJ)/tcp.o

$(OBJ)/rootstorage.o: source/storage/rootstorage.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/storage/rootstorage.c -o $(OBJ)/rootstorage.o

$(OBJ)/rootfs_disk.o: source/fs/rootfs_disk.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/fs/rootfs_disk.c -o $(OBJ)/rootfs_disk.o

$(OBJ)/filesystem.o: source/fs/filesystem.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/fs/filesystem.c -o $(OBJ)/filesystem.o


# ============================================================
# PROCESS / APPLICATION PLATFORM
# ============================================================

$(OBJ)/process.o: source/process/process.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/process/process.c -o $(OBJ)/process.o

$(OBJ)/rootapi.o: source/app/rootapi.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/app/rootapi.c -o $(OBJ)/rootapi.o

$(OBJ)/elf_loader.o: source/app/elf_loader.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/app/elf_loader.c -o $(OBJ)/elf_loader.o

$(OBJ)/app_manager.o: source/app/app_manager.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/app/app_manager.c -o $(OBJ)/app_manager.o


# ============================================================
# PACKAGE MANAGER
# ============================================================

$(OBJ)/rtpgk.o: source/package/rtpgk.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/package/rtpgk.c -o $(OBJ)/rtpgk.o

$(OBJ)/package_manager.o: source/package/package_manager.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/package/package_manager.c -o $(OBJ)/package_manager.o


# ============================================================
# ROOTEDIT / SHELL
# ============================================================

$(OBJ)/rootedit.o: source/editor/rootedit.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/editor/rootedit.c -o $(OBJ)/rootedit.o

$(OBJ)/shell.o: source/shell/shell.c | $(OBJ)
>$(CC) $(CFLAGS) -c source/shell/shell.c -o $(OBJ)/shell.o



# ============================================================
# ROOTDRIVER MODULES / DRIVER STORE
# ============================================================

$(DRIVER_BUILD):
>mkdir -p $(DRIVER_BUILD)

$(E1000_RTDRV): drivers/network/e1000/e1000_driver.c sdk/include/rootos/rootdriver.h | $(DRIVER_BUILD)
>$(CC) $(DRIVER_CFLAGS) -c drivers/network/e1000/e1000_driver.c -o $(E1000_RTDRV)

$(RTL8169_RTDRV): drivers/network/rtl8169/rtl8169_driver.c sdk/include/rootos/rootdriver.h | $(DRIVER_BUILD)
>$(CC) $(DRIVER_CFLAGS) -c drivers/network/rtl8169/rtl8169_driver.c -o $(RTL8169_RTDRV)

$(DRIVERPACK): $(E1000_RTDRV) $(RTL8169_RTDRV) tools/driverpack.py drivers/network/e1000/driver.ini drivers/network/rtl8169/driver.ini | $(BUILD)
>$(PYTHON) tools/driverpack.py --output $(DRIVERPACK) \
	--driver drivers/network/e1000/driver.ini:$(E1000_RTDRV) \
	--driver drivers/network/rtl8169/driver.ini:$(RTL8169_RTDRV)

check-drivers: $(DRIVERPACK)
>@for f in $(E1000_RTDRV) $(RTL8169_RTDRV); do \
	if nm -u $$f | grep -q .; then \
		echo "RootOS: unresolved symbols in $$f"; nm -u $$f; exit 1; \
	fi; \
	done
>@echo "RootOS: RootDriver modules are self-contained ELF32 ET_REL"

# ============================================================
# LINK
# ============================================================

$(BUILD)/kernel.elf: $(OBJECTS) linker.ld | $(BUILD)
>$(LD) \
	-m elf_i386 \
	--build-id=none \
	-z max-page-size=0x1000 \
	-T linker.ld \
	-o $(BUILD)/kernel.elf \
	$(OBJECTS) \
	$(LIBGCC)


# ============================================================
# ISO
# ============================================================

$(BUILD)/RootOS.iso: $(BUILD)/kernel.elf $(DRIVERPACK)
>rm -rf $(ISO_ROOT)
>mkdir -p $(ISO_ROOT)
>cp -a rootfs/. $(ISO_ROOT)/
>mkdir -p $(ISO_ROOT)/boot/grub
>cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
>cp $(DRIVERPACK) $(ISO_ROOT)/boot/driverpack.rdp
>cp grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
>grub-mkrescue \
	-o $(BUILD)/RootOS.iso \
	$(ISO_ROOT)


# ============================================================
# CHECK
# ============================================================

check: $(BUILD)/kernel.elf check-drivers
>@echo "RootOS: checking Multiboot header placement..."
>$(PYTHON) tools/check_multiboot_video.py $(BUILD)/kernel.elf
>@readelf -S $(BUILD)/kernel.elf | grep -E "[.]text|[.]multiboot" || true
>grub-file --is-x86-multiboot $(BUILD)/kernel.elf
>@echo "RootOS: grub-file accepts kernel as Multiboot v1"
>@echo "RootOS: kernel Multiboot + framebuffer valido"


# ============================================================
# PHYSICAL FAT32 LIVE USB
# ============================================================

# Builds the current kernel/driver pack, then creates a real removable disk
# rather than dd'ing the ISO9660 image. Example:
#     make usb-install DEVICE=/dev/sdb
# The installer refuses non-USB disks by default and requires typing ROOTOS.
usb-install: $(BUILD)/kernel.elf $(DRIVERPACK) check
>@if [ -z "$(DEVICE)" ]; then \
	echo "Usage: make usb-install DEVICE=/dev/sdX"; \
	echo "Find the USB with: lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS"; \
	exit 2; \
	fi
>sudo tools/install_live_usb.sh "$(DEVICE)"

# Non-destructive refresh for a USB that was prepared once with usb-install.
usb-update: $(BUILD)/kernel.elf $(DRIVERPACK) check
>@if [ -z "$(DEVICE)" ]; then \
	echo "Usage: make usb-update DEVICE=/dev/sdX"; \
	exit 2; \
	fi
>sudo tools/update_live_usb.sh "$(DEVICE)"


# ============================================================
# ROOTFS42 DEVELOPMENT DISK
# ============================================================

# Always validate before QEMU. A recognized ROOTFS40 image is upgraded
# automatically and a .rootfs40.bak backup is retained.
disk-prepare: tools/mkrootdisk.py
>mkdir -p disk
>$(PYTHON) tools/mkrootdisk.py $(ROOT_DISK) --size-mib 64

# Destructive: explicitly start a fresh ROOTFS42 image.
disk-reset:
>rm -f $(ROOT_DISK)
>mkdir -p disk
>$(PYTHON) tools/mkrootdisk.py $(ROOT_DISK) --size-mib 64 --force


# ============================================================
# ROOTOS APPLICATION SDK / DEMO PACKAGE
# ============================================================

$(DEMO_ELF): examples/apps/hello/hello.c sdk/include/rootos/rootapp.h | $(BUILD)
>mkdir -p $(BUILD)/apps
>$(CC) $(APP_CFLAGS) \
	-c examples/apps/hello/hello.c \
	-o $(DEMO_ELF)

# This is ELF32 ET_REL on purpose. RootOS v0.42 relocates it at runtime.
demo-elf: $(DEMO_ELF)
>@echo "Built demo ELF: $(DEMO_ELF)"

$(DEMO_PACKAGE): $(DEMO_ELF) tools/rtpgk.py | $(BUILD)
>mkdir -p $(BUILD)/packages
>$(PYTHON) tools/rtpgk.py build-app \
	--name Hello \
	--version 1.0 \
	--elf $(DEMO_ELF) \
	--output $(DEMO_PACKAGE)

demo-package: $(DEMO_PACKAGE)
>@echo "Built demo package: $(DEMO_PACKAGE)"

# Host-side development helper: put the local package in RootOS Downloads.
seed-demo: demo-package disk-prepare tools/rootdisk_tool.py
>$(PYTHON) tools/rootdisk_tool.py $(ROOT_DISK) \
	--inject $(DEMO_PACKAGE) /home/user/Downloads/Hello-1.0.rtpgk


# ============================================================
# USB ENUMERATION TEST DISK
# ============================================================

usb-test-disk:
>mkdir -p disk
>$(PYTHON) tools/mkusbtest.py $(USB_TEST_DISK) --size-mib 16


# ============================================================
# RUN ROOTOS
# ============================================================

run: $(BUILD)/RootOS.iso disk-prepare
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

run-xhci: $(BUILD)/RootOS.iso disk-prepare
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci,id=xhci \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Safe USB enumeration test.
# IMPORTANT: do NOT attach usb-kbd/usb-mouse yet. QEMU routes host input to
# those USB devices and overrides PS/2; RootOS v0.44 does not have USB HID
# input transfers yet, so the shell would appear frozen.
#
# This target keeps PS/2 keyboard/mouse active and exposes one USB mass-storage
# device through xHCI. It is enough to validate controller/port/descriptors.
run-usb: $(BUILD)/RootOS.iso disk-prepare usb-test-disk
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-drive if=none,id=usbtest,file=$(USB_TEST_DISK),format=raw \
	-device qemu-xhci,id=xhci \
	-device usb-storage,bus=xhci.0,drive=usbtest,removable=on \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Same safe topology with QEMU diagnostics written to the Linux host.
run-usb-debug: $(BUILD)/RootOS.iso disk-prepare usb-test-disk
>rm -f disk/qemu-usb.log
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-drive if=none,id=usbtest,file=$(USB_TEST_DISK),format=raw \
	-device qemu-xhci,id=xhci \
	-device usb-storage,bus=xhci.0,drive=usbtest,removable=on \
	-d guest_errors,unimp \
	-D disk/qemu-usb.log \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Physical USB passthrough from the Linux host.
# Example after `lsusb` shows "Bus 001 Device 006":
#     make run-usb-host USB_BUS=1 USB_ADDR=6
#
# The selected /dev/bus/usb/BBB/DDD node must be accessible by the current
# user and storage devices must be unmounted on Linux before passthrough.
run-usb-host: $(BUILD)/RootOS.iso disk-prepare
>@if [ -z "$(USB_BUS)" ] || [ -z "$(USB_ADDR)" ] || \
	! printf '%s\n' "$(USB_BUS)" | grep -Eq '^[0-9]+$$' || \
	! printf '%s\n' "$(USB_ADDR)" | grep -Eq '^[0-9]+$$'; then \
		echo "Usage: make run-usb-host USB_BUS=<number> USB_ADDR=<number>"; \
		echo "Example: make run-usb-host USB_BUS=1 USB_ADDR=6"; \
		echo "Find the real numbers with: lsusb"; \
		exit 2; \
	fi
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci,id=xhci \
	-device usb-host,bus=xhci.0,hostbus=$(USB_BUS),hostaddr=$(USB_ADDR) \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Physical RNDIS tethering test.  This is the same USB-host passthrough as
# run-usb-host, named explicitly for Android/Xiaomi network testing.
# Example for "Bus 008 Device 003" from lsusb:
#     make run-rndis-host USB_BUS=8 USB_ADDR=3
run-rndis-host: $(BUILD)/RootOS.iso disk-prepare
>@echo "WARNING: usb-host gives the physical phone exclusively to RootOS/QEMU."
>@echo "Debian will lose its rndis_host network interface until QEMU releases it."
>@if [ -z "$(USB_BUS)" ] || [ -z "$(USB_ADDR)" ] || \
	! printf '%s\n' "$(USB_BUS)" | grep -Eq '^[0-9]+$$' || \
	! printf '%s\n' "$(USB_ADDR)" | grep -Eq '^[0-9]+$$'; then \
		echo "Usage: make run-rndis-host USB_BUS=<number> USB_ADDR=<number>"; \
		echo "Example: make run-rndis-host USB_BUS=8 USB_ADDR=3"; \
		echo "Enable USB tethering and find the current numbers with: lsusb"; \
		exit 2; \
	fi
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci,id=xhci \
	-device usb-host,bus=xhci.0,hostbus=$(USB_BUS),hostaddr=$(USB_ADDR) \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Same physical RNDIS passthrough, with QEMU guest-error logging on Debian.
# This does not capture phone network packets after passthrough, but it does
# preserve xHCI/USB emulation diagnostics when the guest triggers an error.
run-rndis-host-debug: $(BUILD)/RootOS.iso disk-prepare
>@echo "WARNING: usb-host detaches Debian's kernel driver while the guest owns the phone."
>@if [ -z "$(USB_BUS)" ] || [ -z "$(USB_ADDR)" ] || \
	! printf '%s\n' "$(USB_BUS)" | grep -Eq '^[0-9]+$$' || \
	! printf '%s\n' "$(USB_ADDR)" | grep -Eq '^[0-9]+$$'; then \
		echo "Usage: make run-rndis-host-debug USB_BUS=<number> USB_ADDR=<number>"; \
		echo "Enable USB tethering and find the current numbers with: lsusb"; \
		exit 2; \
	fi
>mkdir -p disk
>rm -f disk/qemu-rndis.log
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci,id=xhci \
	-device usb-host,bus=xhci.0,hostbus=$(USB_BUS),hostaddr=$(USB_ADDR) \
	-d guest_errors,unimp \
	-D disk/qemu-rndis.log \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Keep this target only for the future USB HID driver. With v0.44 it will make
# QEMU route keyboard/mouse input to USB, so RootOS cannot use them yet.
run-usb-hid-test: $(BUILD)/RootOS.iso disk-prepare usb-test-disk
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-drive if=none,id=usbtest,file=$(USB_TEST_DISK),format=raw \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0 \
	-device usb-storage,bus=xhci.0,drive=usbtest,removable=on \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Network test: same RootOS.iso, with an Intel e1000 NIC and QEMU user-mode NAT.
run-net: $(BUILD)/RootOS.iso disk-prepare
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-netdev user,id=net0 \
	-device e1000,netdev=net0,mac=52:54:00:12:34:56 \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

# Same virtual NIC, but also capture Ethernet traffic on the Linux host.
# Inspect after QEMU exits with: tcpdump -nn -r disk/rootos-net.pcap
run-net-debug: $(BUILD)/RootOS.iso disk-prepare
>rm -f disk/rootos-net.pcap
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-netdev user,id=net0 \
	-device e1000,netdev=net0,mac=52:54:00:12:34:56 \
	-object filter-dump,id=netdump,netdev=net0,file=disk/rootos-net.pcap \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off

run-demo: seed-demo $(BUILD)/RootOS.iso
>env -u GTK_MODULES \
	qemu-system-i386 \
	-cdrom $(BUILD)/RootOS.iso \
	-drive file=$(ROOT_DISK),format=raw,if=ide,index=0,media=disk \
	-boot d \
	-m 128M \
	-vga std \
	-display gtk,show-cursor=off,grab-on-hover=on,zoom-to-fit=off


# ============================================================
# CLEAN
# ============================================================

clean:
>rm -rf $(BUILD)

clear: clean

rebuild: clean all


.PHONY: \
	all \
	run \
	run-demo \
	run-xhci \
	run-usb \
	run-usb-debug \
	run-usb-host \
	run-rndis-host \
	run-rndis-host-debug \
	run-usb-hid-test \
	run-net \
	run-net-debug \
	check \
	clean \
	clear \
	rebuild \
	disk-prepare \
	disk-reset \
	demo-elf \
	demo-package \
	seed-demo \
	usb-test-disk \
	usb-install \
	usb-update \
	check-drivers
