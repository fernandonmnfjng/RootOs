# RootOs v0.47-6-1 (UNSTABLE)

RootOs is a free, open-source meta-operating system designed from scratch as a structural foundation to help you build and deploy your own custom operating system.

Currently, the custom kernel runs reliably inside the **QEMU** emulator. Native support and boot stability for physical bare-metal hardware (both legacy BIOS and modern UEFI) are actively under development and targeted for the upcoming **v0.5** release.

## v0.47.6-1 UEFI/GOP hotfix

This development hotfix keeps the existing RootDisplay/RootFont graphical terminal and adds support for UEFI GOP framebuffers whose physical address is above the 32-bit 4 GiB boundary.

RootOS remains an i386 kernel. A minimal PAE framebuffer window is enabled only when required by the GOP physical address.

The GRUB configuration prefers `1024x768x32` with automatic fallback and includes a dedicated UEFI video diagnostic boot entry.

---

## Prerequisites (Development Environment)

```bash
sudo apt update
sudo apt install build-essential qemu-system-x86 grub-pc-bin xorriso mtools
```

## Compilation

```bash
make clean
make
make check
make run
```

## Physical USB update

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
make usb-update DEVICE=/dev/sdX
```

## License

This project is released under the **CC0 1.0 Universal (Creative Commons Zero)** Public Domain Dedication.
