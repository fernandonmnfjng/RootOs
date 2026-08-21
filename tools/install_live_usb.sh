#!/usr/bin/env bash
set -euo pipefail

# RootOS FAT32 Live USB installer.
# Destructive: creates a bootable MBR+FAT32 removable medium.
# It intentionally copies ONLY boot files. The current RootOS VFS does not
# mount this FAT partition as /, so mirroring rootfs/ here is unnecessary.

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE="${1:-}"
ALLOW_NON_USB="${ROOTOS_ALLOW_NON_USB:-0}"
MOUNT_DIR=""

fail() { printf 'RootOS USB: ERROR: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "missing host tool: $1"; }
cleanup() {
    if [[ -n "$MOUNT_DIR" && -d "$MOUNT_DIR" ]]; then
        if mountpoint -q "$MOUNT_DIR" 2>/dev/null; then umount "$MOUNT_DIR" || true; fi
        rmdir "$MOUNT_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

[[ $EUID -eq 0 ]] || fail "run through: make usb-install DEVICE=/dev/sdX (it uses sudo)"
[[ -n "$DEVICE" ]] || fail "usage: $0 /dev/sdX"
[[ -b "$DEVICE" ]] || fail "$DEVICE is not a block device"

for cmd in lsblk findmnt parted partprobe udevadm wipefs mkfs.vfat mount mountpoint umount grub-install sync cp; do
    need "$cmd"
done

TYPE="$(lsblk -ndo TYPE "$DEVICE" 2>/dev/null | head -n1 || true)"
[[ "$TYPE" == "disk" ]] || fail "$DEVICE is not a whole disk (TYPE=$TYPE)"

case "$DEVICE" in
    /dev/nvme*|/dev/mmcblk*|/dev/loop*|/dev/md*|/dev/dm-*)
        [[ "$ALLOW_NON_USB" == "1" ]] || fail "refusing high-risk/non-USB device $DEVICE"
        ;;
esac

TRAN="$(lsblk -ndo TRAN "$DEVICE" 2>/dev/null | head -n1 || true)"
if [[ "$TRAN" != "usb" && "$ALLOW_NON_USB" != "1" ]]; then
    fail "$DEVICE does not report TRAN=usb (reported '${TRAN:-unknown}')"
fi

ROOT_SOURCE="$(findmnt -n -o SOURCE / 2>/dev/null || true)"
if [[ -n "$ROOT_SOURCE" ]] && lsblk -sno PATH "$ROOT_SOURCE" 2>/dev/null | grep -Fxq "$DEVICE"; then
    fail "$DEVICE backs the currently running root filesystem"
fi

[[ -f "$PROJECT_ROOT/build/kernel.elf" ]] || fail "build/kernel.elf missing; run make first"
[[ -f "$PROJECT_ROOT/build/driverpack.rdp" ]] || fail "build/driverpack.rdp missing; run make first"
[[ -f "$PROJECT_ROOT/grub/grub.cfg" ]] || fail "grub/grub.cfg missing"

[[ -d /usr/lib/grub/i386-pc ]] || fail "BIOS GRUB missing: apt install grub-pc-bin"
[[ -d /usr/lib/grub/x86_64-efi ]] || fail "x86_64 UEFI GRUB missing: apt install grub-efi-amd64-bin"

printf '\nRootOS FAT32 Live USB installer\n================================\n'
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,FSTYPE,MOUNTPOINTS "$DEVICE"
printf '\nTHIS ERASES THE ENTIRE DEVICE: %s\n' "$DEVICE"
printf 'Creates: MBR + 1 MiB GRUB gap + FAT32 ESP + BIOS/UEFI GRUB.\n'
printf 'Only RootOS boot files are copied; no Unix ownership is preserved on FAT32.\n'
printf 'No backup will be created.\n\nType ROOTOS to continue: '
read -r ANSWER
[[ "$ANSWER" == "ROOTOS" ]] || fail "cancelled"

while read -r NODE MNT; do
    [[ -n "${MNT:-}" ]] || continue
    printf 'Unmounting %s from %s\n' "$NODE" "$MNT"
    umount "$NODE"
done < <(lsblk -lnpo NAME,MOUNTPOINT "$DEVICE" | tail -n +2)

printf '[1/7] Clearing old signatures...\n'
wipefs -a "$DEVICE"

printf '[2/7] Creating MBR + 1 MiB embedding gap + FAT32 ESP...\n'
parted -s "$DEVICE" mklabel msdos
parted -s "$DEVICE" mkpart primary fat32 1MiB 100%
parted -s "$DEVICE" set 1 esp on
parted -s "$DEVICE" set 1 boot on
partprobe "$DEVICE"
udevadm settle

PART="$(lsblk -lnpo NAME,TYPE "$DEVICE" | awk '$2 == "part" {print $1; exit}')"
[[ -n "$PART" && -b "$PART" ]] || fail "could not discover the new partition"

while read -r NODE MNT; do
    [[ -n "${MNT:-}" ]] || continue
    umount "$NODE"
done < <(lsblk -lnpo NAME,MOUNTPOINT "$DEVICE" | tail -n +2)

printf '[3/7] Formatting %s as FAT32...\n' "$PART"
mkfs.vfat -F 32 -n ROOTOS "$PART"

MOUNT_DIR="$(mktemp -d /tmp/rootos-usb.XXXXXX)"
mount "$PART" "$MOUNT_DIR"

printf '[4/7] Creating minimal RootOS boot layout...\n'
mkdir -p "$MOUNT_DIR/boot/grub" "$MOUNT_DIR/EFI/BOOT"
# Plain cp is intentional. FAT32 has no POSIX uid/gid/mode/xattr semantics.
cp -f "$PROJECT_ROOT/build/kernel.elf" "$MOUNT_DIR/boot/kernel.elf"
cp -f "$PROJECT_ROOT/build/driverpack.rdp" "$MOUNT_DIR/boot/driverpack.rdp"

printf '[5/7] Installing legacy BIOS GRUB...\n'
grub-install \
    --target=i386-pc \
    --boot-directory="$MOUNT_DIR/boot" \
    --recheck \
    "$DEVICE"

printf '[6/7] Installing x86_64 UEFI removable GRUB...\n'
grub-install \
    --target=x86_64-efi \
    --efi-directory="$MOUNT_DIR" \
    --boot-directory="$MOUNT_DIR/boot" \
    --removable \
    --no-nvram \
    --recheck

if [[ -d /usr/lib/grub/i386-efi ]]; then
    printf '      Installing IA32 UEFI fallback too...\n'
    grub-install \
        --target=i386-efi \
        --efi-directory="$MOUNT_DIR" \
        --boot-directory="$MOUNT_DIR/boot" \
        --removable \
        --no-nvram \
        --recheck
else
    printf '      NOTE: IA32 UEFI skipped (install grub-efi-ia32-bin if desired).\n'
fi

# grub-install may modify boot/grub; install RootOS config last, using plain cp.
cp -f "$PROJECT_ROOT/grub/grub.cfg" "$MOUNT_DIR/boot/grub/grub.cfg"

printf '[7/7] Verifying removable-media files...\n'
[[ -f "$MOUNT_DIR/EFI/BOOT/BOOTX64.EFI" ]] || fail "BOOTX64.EFI was not created"
[[ -f "$MOUNT_DIR/boot/kernel.elf" ]] || fail "kernel copy missing"
[[ -f "$MOUNT_DIR/boot/driverpack.rdp" ]] || fail "driverpack copy missing"
[[ -f "$MOUNT_DIR/boot/grub/grub.cfg" ]] || fail "grub.cfg copy missing"
if [[ -d /usr/lib/grub/i386-efi ]]; then
    [[ -f "$MOUNT_DIR/EFI/BOOT/BOOTIA32.EFI" ]] || fail "BOOTIA32.EFI missing"
fi

sync
umount "$MOUNT_DIR"
rmdir "$MOUNT_DIR"
MOUNT_DIR=""

printf '\nRootOS Live USB ready: %s\n' "$DEVICE"
printf '  Filesystem : FAT32\n'
printf '  BIOS       : GRUB i386-pc in MBR/embedding gap\n'
printf '  UEFI x64   : /EFI/BOOT/BOOTX64.EFI\n'
if [[ -d /usr/lib/grub/i386-efi ]]; then printf '  UEFI IA32  : /EFI/BOOT/BOOTIA32.EFI\n'; fi
printf '  RootOS     : /boot/kernel.elf + /boot/driverpack.rdp\n'
printf '  Boot menu  : normal + safe\n'
printf '  Secure Boot: disabled for this unsigned development build\n'
printf '\nFuture builds can be copied without repartitioning with:\n'
printf '  make usb-update DEVICE=%s\n' "$DEVICE"
