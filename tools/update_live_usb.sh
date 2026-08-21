#!/usr/bin/env bash
set -euo pipefail

# Non-destructive RootOS Live USB updater.
# Does not repartition, format, reinstall GRUB, or create backups.

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE="${1:-}"
MOUNT_DIR=""

fail() { printf 'RootOS USB update: ERROR: %s\n' "$*" >&2; exit 1; }
cleanup() {
    if [[ -n "$MOUNT_DIR" && -d "$MOUNT_DIR" ]]; then
        if mountpoint -q "$MOUNT_DIR" 2>/dev/null; then umount "$MOUNT_DIR" || true; fi
        rmdir "$MOUNT_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

[[ $EUID -eq 0 ]] || fail "run through make usb-update DEVICE=/dev/sdX"
[[ -n "$DEVICE" && -b "$DEVICE" ]] || fail "usage: $0 /dev/sdX"
[[ "$(lsblk -ndo TYPE "$DEVICE" | head -n1)" == "disk" ]] || fail "$DEVICE is not a whole disk"
[[ "$(lsblk -ndo TRAN "$DEVICE" | head -n1)" == "usb" ]] || fail "$DEVICE does not report TRAN=usb"

[[ -f "$PROJECT_ROOT/build/kernel.elf" ]] || fail "build/kernel.elf missing"
[[ -f "$PROJECT_ROOT/build/driverpack.rdp" ]] || fail "build/driverpack.rdp missing"
[[ -f "$PROJECT_ROOT/grub/grub.cfg" ]] || fail "grub/grub.cfg missing"

PART="$(lsblk -lnpo NAME,TYPE,FSTYPE "$DEVICE" | awk '$2 == "part" && ($3 == "vfat" || $3 == "fat" || $3 == "fat32") {print $1; exit}')"
[[ -n "$PART" ]] || fail "no FAT partition found on $DEVICE; run usb-install once first"

while read -r NODE MNT; do
    [[ -n "${MNT:-}" ]] || continue
    umount "$NODE"
done < <(lsblk -lnpo NAME,MOUNTPOINT "$DEVICE" | tail -n +2)

MOUNT_DIR="$(mktemp -d /tmp/rootos-usb-update.XXXXXX)"
mount "$PART" "$MOUNT_DIR"
[[ -d "$MOUNT_DIR/EFI/BOOT" ]] || fail "EFI/BOOT missing; this does not look like a prepared RootOS USB"
[[ -d "$MOUNT_DIR/boot/grub" ]] || fail "boot/grub missing; run usb-install once first"

printf 'Updating RootOS boot files on %s (%s)...\n' "$DEVICE" "$PART"
cp -f "$PROJECT_ROOT/build/kernel.elf" "$MOUNT_DIR/boot/kernel.elf"
cp -f "$PROJECT_ROOT/build/driverpack.rdp" "$MOUNT_DIR/boot/driverpack.rdp"
cp -f "$PROJECT_ROOT/grub/grub.cfg" "$MOUNT_DIR/boot/grub/grub.cfg"
sync
umount "$MOUNT_DIR"
rmdir "$MOUNT_DIR"
MOUNT_DIR=""
printf 'RootOS USB updated without formatting or reinstalling GRUB.\n'
